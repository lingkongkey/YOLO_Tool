# -*- coding: utf-8 -*-
"""将 YOLO 检测散图数据集离线打包为未压缩 tar shard 和二进制索引。

用法：
    python pack_dataset.py --data data.yaml --output packed_dataset
    python pack_dataset.py --data data.yaml --output packed_dataset --resume

输入 data.yaml 的 train/val 必须是图片目录；标签采用标准 YOLO 五列格式。
首次只遍历一次原图目录并建立源文件清单；中断后续传不重新扫描原图目录。
"""

import argparse
import io
import json
import math
import os
import shutil
import sqlite3
import sys
import tarfile
import time
import zlib
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import cv2
import numpy as np
from ultralytics.utils import TQDM, YAML

from keyolo_tar import (
    CODEC_IDS,
    DATASET_FORMAT,
    INDEX_RECORD_SIZE,
    ShardIndex,
    file_sha256,
    write_index,
)

IMAGE_SUFFIXES = set(CODEC_IDS)
FLAG_MISSING_LABEL = 1
CHECKPOINT_FORMAT = "keyolo-pack-checkpoint"
APPEND_CHECKPOINT_FORMAT = "keyolo-append-checkpoint"
INVENTORY_FORMAT = "keyolo-source-inventory"
STATE_VERSION = 1


class IncrementalConflict(ValueError):
    """增量追加发现源图片/标签被修改或删除，需要整包重建。"""


def atomic_json(path, value):
    tmp_path = path.with_suffix(path.suffix + ".tmp")
    with open(tmp_path, "w", encoding="utf-8") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(tmp_path, path)


def load_data_yaml(data_path):
    data = YAML.load(data_path)
    if not isinstance(data, dict):
        raise ValueError("data.yaml 内容无效")

    dataset_root = Path(data.get("path") or data_path.parent)
    if not dataset_root.is_absolute():
        dataset_root = (data_path.parent / dataset_root).resolve()

    names = data.get("names")
    if isinstance(names, dict):
        names = [names.get(i, names.get(str(i))) for i in range(len(names))]
    if not isinstance(names, list) or not names or any(name is None for name in names):
        raise ValueError("data.yaml 必须提供非空 names 列表")

    splits = {}
    for split in ("train", "val"):
        value = data.get(split)
        if not isinstance(value, str) or not value:
            raise ValueError(f"data.yaml 的 {split} 必须是单个图片目录")
        image_dir = Path(value)
        if not image_dir.is_absolute():
            image_dir = dataset_root / image_dir
        image_dir = image_dir.resolve()
        if not image_dir.is_dir():
            raise FileNotFoundError(f"{split} 图片目录不存在: {image_dir}")
        splits[split] = image_dir
    return splits, names


def labels_root_for(images_root):
    parts = list(images_root.parts)
    for i in range(len(parts) - 1, -1, -1):
        if parts[i].lower() == "images":
            parts[i] = "labels"
            return Path(*parts)
    return images_root.parent / "labels"


def parse_yolo_label(label_path, class_count):
    if not label_path.is_file():
        return b"", 0, FLAG_MISSING_LABEL, []

    text = label_path.read_text(encoding="utf-8-sig").strip()
    if not text:
        return b"", 0, 0, []

    normalized_lines = []
    class_ids = []
    for line_no, line in enumerate(text.splitlines(), 1):
        fields = line.split()
        if len(fields) != 5:
            raise ValueError(f"{label_path}:{line_no} 应为 class x y w h 五列")
        try:
            class_value = float(fields[0])
            class_id = int(class_value)
            box = [float(value) for value in fields[1:]]
        except ValueError as exc:
            raise ValueError(f"{label_path}:{line_no} 包含非数字字段") from exc

        if class_value != class_id or not 0 <= class_id < class_count:
            raise ValueError(f"{label_path}:{line_no} 类别 ID 越界: {fields[0]}")
        if not all(math.isfinite(value) and 0.0 <= value <= 1.0 for value in box):
            raise ValueError(f"{label_path}:{line_no} 坐标必须在 [0, 1] 内")
        if box[2] <= 0.0 or box[3] <= 0.0:
            raise ValueError(f"{label_path}:{line_no} 宽高必须大于 0")

        class_ids.append(class_id)
        normalized_lines.append(f"{class_id} {' '.join(fields[1:])}")

    annotation = ("\n".join(normalized_lines) + "\n").encode("utf-8")
    return annotation, len(class_ids), 0, class_ids


def add_member(tf, raw, name, payload):
    header_offset = raw.tell()
    info = tarfile.TarInfo(name)
    info.size = len(payload)
    info.mtime = 0
    info.mode = 0o644
    tf.addfile(info, io.BytesIO(payload))
    # member 名固定为短 ASCII，USTAR 不会产生 PAX 扩展头
    return header_offset + tarfile.BLOCKSIZE


def file_version(path):
    try:
        stat = path.stat()
    except FileNotFoundError:
        return -1, -1
    return stat.st_size, stat.st_mtime_ns


def row_from_record(record):
    return {
        "sample_id": record.sample_id,
        "image_offset": record.image_offset,
        "image_size": record.image_size,
        "label_offset": record.label_offset,
        "label_size": record.label_size,
        "annotation": record.annotation,
        "width": record.width,
        "height": record.height,
        "crc32": record.crc32,
        "codec": record.codec,
        "flags": record.flags,
    }


def rows_tar_size(rows):
    last = rows[-1]
    return last["label_offset"] + ((last["label_size"] + 511) // 512) * 512


class ShardWriter:
    def __init__(self, split_root, shard_id):
        self.split_root = split_root
        self.shard_id = shard_id
        self.tar_path = split_root / "shards" / f"{shard_id:06d}.tar"
        self.index_path = split_root / "index" / f"{shard_id:06d}.idx"
        self.rows = []
        self.raw = open(self.tar_path, "wb")
        self.tf = tarfile.open(fileobj=self.raw, mode="w", format=tarfile.USTAR_FORMAT)

    @classmethod
    def resume(cls, split_root, state):
        """按最后检查点截断未提交字节，并恢复当前分片的索引行。"""
        shard_id = state["shard_id"]
        tar_path = split_root / "shards" / f"{shard_id:06d}.tar"
        index_path = split_root / "index" / f"{shard_id:06d}.idx"
        sample_count = state["sample_count"]
        if sample_count <= 0:
            raise ValueError(f"检查点中的当前分片为空: shard={shard_id}")

        index = ShardIndex(index_path, shard_id)
        try:
            if index.count < sample_count:
                raise ValueError(
                    f"分片索引早于检查点: shard={shard_id}, "
                    f"index={index.count}, checkpoint={sample_count}"
                )
            rows = [row_from_record(index.record(i)) for i in range(sample_count)]
        finally:
            index.close()
        if index.count > sample_count:
            write_index(index_path, shard_id, rows)

        expected_size = rows_tar_size(rows)
        if state["tar_bytes"] != expected_size:
            raise ValueError(
                f"检查点分片长度无效: shard={shard_id}, "
                f"checkpoint={state['tar_bytes']}, expected={expected_size}"
            )

        writer = cls.__new__(cls)
        writer.split_root = split_root
        writer.shard_id = shard_id
        writer.tar_path = tar_path
        writer.index_path = index_path
        writer.rows = rows
        writer.raw = open(tar_path, "r+b")
        try:
            actual_size = writer.raw.seek(0, os.SEEK_END)
            if actual_size < expected_size:
                raise ValueError(
                    f"分片文件早于检查点: {tar_path}, actual={actual_size}, expected={expected_size}"
                )
            writer.raw.truncate(expected_size)
            writer.raw.seek(expected_size)
            writer.raw.flush()
            os.fsync(writer.raw.fileno())
            writer.tf = tarfile.open(fileobj=writer.raw, mode="w", format=tarfile.USTAR_FORMAT)
        except Exception:
            writer.raw.close()
            raise
        return writer

    def add(self, sample_id, relative_path, image_bytes, annotation, width, height, codec, flags):
        key = f"{sample_id:016x}"
        suffix = next(suffix for suffix, value in CODEC_IDS.items() if value == codec)
        image_offset = add_member(self.tf, self.raw, f"{key}{suffix}", image_bytes)
        label_json = json.dumps(
            {
                "source": relative_path.as_posix(),
                "width": width,
                "height": height,
                "annotation": annotation.decode("utf-8"),
            },
            ensure_ascii=False,
            separators=(",", ":"),
        ).encode("utf-8")
        label_offset = add_member(self.tf, self.raw, f"{key}.json", label_json)

        self.rows.append({
            "sample_id": sample_id,
            "image_offset": image_offset,
            "image_size": len(image_bytes),
            "label_offset": label_offset,
            "label_size": len(label_json),
            "annotation": annotation,
            "width": width,
            "height": height,
            "crc32": zlib.crc32(image_bytes),
            "codec": codec,
            "flags": flags,
        })

    def checkpoint(self):
        """先持久化 tar 和索引，再由调用方原子发布检查点。"""
        if not self.rows:
            raise ValueError("不能保存空分片检查点")
        self.raw.flush()
        os.fsync(self.raw.fileno())
        tar_bytes = self.raw.tell()
        if tar_bytes != rows_tar_size(self.rows):
            raise IOError(f"分片写入长度不一致: {self.tar_path}")
        write_index(self.index_path, self.shard_id, self.rows)
        return {
            "shard_id": self.shard_id,
            "sample_count": len(self.rows),
            "tar_bytes": tar_bytes,
        }

    def close(self):
        if not self.rows:
            raise ValueError("不能关闭空分片")
        self.raw.flush()
        self.tf.close()
        self.raw.flush()
        os.fsync(self.raw.fileno())
        self.raw.close()
        index_sha256 = write_index(self.index_path, self.shard_id, self.rows)
        tar_sha256 = file_sha256(self.tar_path)
        return {
            "shard_id": self.shard_id,
            "path": self.tar_path.relative_to(self.split_root).as_posix(),
            "index": self.index_path.relative_to(self.split_root).as_posix(),
            "sample_count": len(self.rows),
            "active_count": len(self.rows),
            "bytes": self.tar_path.stat().st_size,
            "sha256": tar_sha256,
            "index_sha256": index_sha256,
        }


class SplitPacker:
    def __init__(
        self,
        split,
        images_root,
        output_root,
        class_count,
        shard_size,
        shard_bytes,
        checkpoint_interval,
    ):
        self.split = split
        self.images_root = images_root
        self.labels_root = labels_root_for(images_root)
        self.output_root = output_root
        self.class_count = class_count
        self.shard_size = shard_size
        self.shard_bytes = shard_bytes
        self.checkpoint_interval = checkpoint_interval
        self.inventory_path = output_root / "sources.jsonl"
        self.checkpoint_path = output_root / "checkpoint.json"
        self.signature = {
            "images_root": str(images_root),
            "labels_root": str(self.labels_root),
            "class_count": class_count,
            "shard_size": shard_size,
            "shard_bytes": shard_bytes,
        }
        self.class_counts = [0] * class_count
        self.max_objects = 0
        self.sample_id = 0
        self.total_samples = 0
        self.shards = []

    def _read_inventory_header(self, stream, path):
        line = stream.readline()
        if not line:
            raise ValueError(f"源文件清单为空: {path}")
        header = json.loads(line)
        if (
            header.get("format") != INVENTORY_FORMAT
            or header.get("version") != STATE_VERSION
            or header.get("signature") != self.signature
        ):
            raise ValueError(f"源文件清单与当前打包参数不匹配: {path}")

    def _iter_inventory(self, path=None):
        path = path or self.inventory_path
        with open(path, "r", encoding="utf-8") as stream:
            self._read_inventory_header(stream, path)
            for line_no, line in enumerate(stream, 2):
                entry = json.loads(line)
                if not isinstance(entry, list) or len(entry) != 5:
                    raise ValueError(f"{path}:{line_no} 清单记录无效")
                yield entry

    def _write_inventory(self, path):
        tmp_path = path.with_suffix(path.suffix + ".tmp")
        count = 0
        progress = TQDM(desc=f"{self.split} 扫描", unit="张", mininterval=0.5)
        try:
            with open(tmp_path, "w", encoding="utf-8", newline="\n") as stream:
                header = {
                    "format": INVENTORY_FORMAT,
                    "version": STATE_VERSION,
                    "signature": self.signature,
                }
                stream.write(json.dumps(header, ensure_ascii=False, separators=(",", ":")) + "\n")
                for root, dirs, files in os.walk(self.images_root):
                    dirs.sort()
                    files.sort()
                    for filename in files:
                        image_path = Path(root) / filename
                        if image_path.suffix.lower() not in IMAGE_SUFFIXES:
                            continue
                        relative_path = image_path.relative_to(self.images_root)
                        label_path = self.labels_root / relative_path.with_suffix(".txt")
                        image_size, image_mtime = file_version(image_path)
                        label_size, label_mtime = file_version(label_path)
                        entry = [
                            relative_path.as_posix(),
                            image_size,
                            image_mtime,
                            label_size,
                            label_mtime,
                        ]
                        stream.write(json.dumps(entry, ensure_ascii=False, separators=(",", ":")) + "\n")
                        count += 1
                        progress.update(1)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(tmp_path, path)
        finally:
            progress.close()
        return count

    def _ensure_inventory(self):
        self.output_root.mkdir(parents=True, exist_ok=True)
        if self.inventory_path.is_file():
            return sum(1 for _ in self._iter_inventory())
        return self._write_inventory(self.inventory_path)

    def _load_state(self):
        if not self.checkpoint_path.is_file():
            if self.total_samples == 0:
                raise ValueError(f"没有找到支持的图片: {self.images_root}")
            self._save_checkpoint()
            return None

        with open(self.checkpoint_path, "r", encoding="utf-8") as stream:
            state = json.load(stream)
        if (
            state.get("format") != CHECKPOINT_FORMAT
            or state.get("version") != STATE_VERSION
            or state.get("split") != self.split
            or state.get("signature") != self.signature
        ):
            raise ValueError(f"检查点与当前数据集或打包参数不匹配: {self.checkpoint_path}")
        if state.get("total_samples") != self.total_samples:
            raise ValueError("检查点总样本数与源文件清单不一致")

        self.sample_id = state["sample_id"]
        self.class_counts = state["class_counts"]
        self.max_objects = state["max_objects_per_image"]
        self.shards = state["shards"]
        current = state.get("current_shard")
        if len(self.class_counts) != self.class_count:
            raise ValueError("检查点类别统计长度无效")
        if any(shard["shard_id"] != i for i, shard in enumerate(self.shards)):
            raise ValueError("检查点分片编号不连续")
        completed_count = sum(shard["sample_count"] for shard in self.shards)
        current_count = current["sample_count"] if current else 0
        if current and current["shard_id"] != len(self.shards):
            raise ValueError("检查点当前分片编号无效")
        if self.sample_id != completed_count + current_count:
            raise ValueError("检查点样本计数不一致")
        if not 0 <= self.sample_id <= self.total_samples:
            raise ValueError("检查点样本序号越界")
        if self.sample_id:
            print(
                f"[INFO] {self.split}: 从检查点恢复，已提交 {self.sample_id}/"
                f"{self.total_samples} 张，完成 {len(self.shards)} 个分片",
                flush=True,
            )
        return current

    def _save_checkpoint(self, writer=None):
        state = {
            "format": CHECKPOINT_FORMAT,
            "version": STATE_VERSION,
            "split": self.split,
            "signature": self.signature,
            "total_samples": self.total_samples,
            "sample_id": self.sample_id,
            "class_counts": self.class_counts,
            "max_objects_per_image": self.max_objects,
            "shards": self.shards,
            "current_shard": writer.checkpoint() if writer is not None else None,
        }
        atomic_json(self.checkpoint_path, state)

    def _verify_source(self, entry):
        relative_path = Path(entry[0])
        image_path = self.images_root / relative_path
        label_path = self.labels_root / relative_path.with_suffix(".txt")
        if file_version(image_path) != (entry[1], entry[2]):
            raise ValueError(f"源图片在清单生成后已变化，拒绝混用版本: {image_path}")
        if file_version(label_path) != (entry[3], entry[4]):
            raise ValueError(f"源标签在清单生成后已变化，拒绝混用版本: {label_path}")
        return image_path, relative_path, label_path

    def pack(self):
        (self.output_root / "shards").mkdir(parents=True, exist_ok=True)
        (self.output_root / "index").mkdir(parents=True, exist_ok=True)
        self.total_samples = self._ensure_inventory()
        current = self._load_state()
        writer = ShardWriter.resume(self.output_root, current) if current else None

        progress = TQDM(
            total=self.total_samples,
            initial=self.sample_id,
            desc=f"{self.split} 打包",
            unit="张",
            mininterval=0.2,
        )
        try:
            for source_index, entry in enumerate(self._iter_inventory()):
                image_path, relative_path, label_path = self._verify_source(entry)
                if source_index < self.sample_id:
                    continue
                if source_index != self.sample_id:
                    raise ValueError("源文件清单位置与检查点不一致")

                image_bytes = image_path.read_bytes()
                array = np.frombuffer(image_bytes, dtype=np.uint8)
                image = cv2.imdecode(array, cv2.IMREAD_UNCHANGED)
                if image is None:
                    raise ValueError(f"图片解码失败: {image_path}")
                height, width = image.shape[:2]
                annotation, object_count, flags, class_ids = parse_yolo_label(
                    label_path, self.class_count
                )
                for class_id in class_ids:
                    self.class_counts[class_id] += 1
                self.max_objects = max(self.max_objects, object_count)

                if writer is None:
                    writer = ShardWriter(self.output_root, len(self.shards))
                writer.add(
                    self.sample_id,
                    relative_path,
                    image_bytes,
                    annotation,
                    width,
                    height,
                    CODEC_IDS[image_path.suffix.lower()],
                    flags,
                )
                self.sample_id += 1
                progress.update(1)

                if len(writer.rows) >= self.shard_size or writer.raw.tell() >= self.shard_bytes:
                    self.shards.append(writer.close())
                    writer = None
                    self._save_checkpoint()
                elif self.sample_id % self.checkpoint_interval == 0:
                    self._save_checkpoint(writer)

            if writer is not None:
                self.shards.append(writer.close())
                writer = None
                self._save_checkpoint()
            if self.sample_id == 0:
                raise ValueError(f"没有找到支持的图片: {self.images_root}")
            if self.sample_id != self.total_samples:
                raise ValueError(
                    f"源文件清单不完整: 已处理 {self.sample_id}, 清单共 {self.total_samples}"
                )
        finally:
            progress.close()

        manifest = {
            "format": DATASET_FORMAT,
            "version": 1,
            "generation": 1,
            "compression": "none",
            "tar_format": "ustar",
            "index_format": "keyolo-idx-v1",
            "record_size": INDEX_RECORD_SIZE,
            "total_samples": self.sample_id,
            "active_samples": self.sample_id,
            "next_sample_id": self.sample_id,
            "shard_count": len(self.shards),
            "class_counts": self.class_counts,
            "max_objects_per_image": self.max_objects,
            "shards": self.shards,
        }
        atomic_json(self.output_root / "manifest.json", manifest)
        self.checkpoint_path.unlink(missing_ok=True)
        # 保留源快照，后续训练可据此只追加新增图片，无需重打旧分片。
        return manifest


class IncrementalPacker(SplitPacker):
    """保留旧分片，只把新增源图片写入后续分片。"""

    def __init__(
        self,
        split,
        images_root,
        output_root,
        class_count,
        shard_size,
        shard_bytes,
        checkpoint_interval,
        manifest,
    ):
        super().__init__(
            split,
            images_root,
            output_root,
            class_count,
            shard_size,
            shard_bytes,
            checkpoint_interval,
        )
        self.manifest_path = output_root / "manifest.json"
        self.current_inventory_path = output_root / "sources.current.jsonl"
        self.append_inventory_path = output_root / "append.jsonl"
        self.diff_path = output_root / "inventory_diff.sqlite"
        self.checkpoint_path = output_root / "append_checkpoint.json"
        self.base_manifest = manifest
        self.append_count = 0
        self.inventory_sha256 = None
        self._validate_manifest(manifest)

    def _validate_manifest(self, manifest):
        shards = manifest.get("shards")
        total = manifest.get("total_samples")
        if manifest.get("format") != DATASET_FORMAT or manifest.get("compression") != "none":
            raise ValueError(f"不是支持的未压缩 tar 数据集: {self.manifest_path}")
        if not isinstance(total, int) or total <= 0 or not isinstance(shards, list) or not shards:
            raise ValueError(f"manifest 样本或分片信息无效: {self.manifest_path}")
        if manifest.get("active_samples") != total or manifest.get("next_sample_id") != total:
            raise ValueError("逻辑删除或自定义样本序号的数据集暂不支持增量追加")
        if manifest.get("shard_count") != len(shards):
            raise ValueError("manifest 分片计数不一致")
        if any(shard.get("shard_id") != i for i, shard in enumerate(shards)):
            raise ValueError("manifest 分片编号不连续")
        if sum(shard.get("sample_count", 0) for shard in shards) != total:
            raise ValueError("manifest 总样本数与分片统计不一致")
        if len(manifest.get("class_counts", [])) != self.class_count:
            raise ValueError("manifest 类别统计与 data.yaml 不一致")

    def _checkpoint_state(self, writer=None):
        return {
            "format": APPEND_CHECKPOINT_FORMAT,
            "version": STATE_VERSION,
            "split": self.split,
            "signature": self.signature,
            "base_manifest": self.base_manifest,
            "total_samples": self.total_samples,
            "append_count": self.append_count,
            "sample_id": self.sample_id,
            "class_counts": self.class_counts,
            "max_objects_per_image": self.max_objects,
            "shards": self.shards,
            "current_shard": writer.checkpoint() if writer is not None else None,
            "inventory_sha256": self.inventory_sha256,
        }

    def _save_checkpoint(self, writer=None):
        state = self._checkpoint_state(writer)
        atomic_json(self.checkpoint_path, state)
        return state

    def _validate_checkpoint(self, state):
        if (
            state.get("format") != APPEND_CHECKPOINT_FORMAT
            or state.get("version") != STATE_VERSION
            or state.get("split") != self.split
            or state.get("signature") != self.signature
        ):
            raise ValueError(f"追加检查点与当前数据集或参数不匹配: {self.checkpoint_path}")
        base = state.get("base_manifest")
        if not isinstance(base, dict):
            raise ValueError("追加检查点缺少基础 manifest")
        self._validate_manifest(base)

        append_count = state.get("append_count")
        total = state.get("total_samples")
        sample_id = state.get("sample_id")
        base_total = base["total_samples"]
        if not isinstance(append_count, int) or append_count <= 0:
            raise ValueError("追加检查点的新增样本数无效")
        if total != base_total + append_count or not base_total <= sample_id <= total:
            raise ValueError("追加检查点的样本序号越界")
        if not isinstance(state.get("inventory_sha256"), str):
            raise ValueError("追加检查点缺少源快照校验值")

        shards = state.get("shards")
        if not isinstance(shards, list) or shards[: len(base["shards"])] != base["shards"]:
            raise ValueError("追加检查点改变了基础分片")
        if any(shard.get("shard_id") != i for i, shard in enumerate(shards)):
            raise ValueError("追加检查点分片编号不连续")
        current = state.get("current_shard")
        current_count = current["sample_count"] if current else 0
        if current and current.get("shard_id") != len(shards):
            raise ValueError("追加检查点当前分片编号无效")
        if sample_id != sum(shard["sample_count"] for shard in shards) + current_count:
            raise ValueError("追加检查点样本计数不一致")
        if len(state.get("class_counts", [])) != self.class_count:
            raise ValueError("追加检查点类别统计长度无效")

    @staticmethod
    def _manifest_from_state(state):
        base = state["base_manifest"]
        manifest = dict(base)
        manifest.update({
            "generation": base.get("generation", 1) + 1,
            "total_samples": state["sample_id"],
            "active_samples": state["sample_id"],
            "next_sample_id": state["sample_id"],
            "shard_count": len(state["shards"]),
            "class_counts": state["class_counts"],
            "max_objects_per_image": state["max_objects_per_image"],
            "shards": state["shards"],
        })
        return manifest

    def _restore_state(self, state):
        self.base_manifest = state["base_manifest"]
        self.total_samples = state["total_samples"]
        self.append_count = state["append_count"]
        self.sample_id = state["sample_id"]
        self.class_counts = state["class_counts"]
        self.max_objects = state["max_objects_per_image"]
        self.shards = state["shards"]
        self.inventory_sha256 = state["inventory_sha256"]

    def _cleanup_transients(self):
        paths = (
            self.checkpoint_path,
            self.current_inventory_path,
            self.append_inventory_path,
            self.diff_path,
            self.current_inventory_path.with_suffix(
                self.current_inventory_path.suffix + ".tmp"
            ),
            self.append_inventory_path.with_suffix(self.append_inventory_path.suffix + ".tmp"),
        )
        for path in paths:
            path.unlink(missing_ok=True)

    def _publish_snapshot(self, state):
        expected = state["inventory_sha256"]
        if (
            self.current_inventory_path.is_file()
            and file_sha256(self.current_inventory_path) == expected
        ):
            os.replace(self.current_inventory_path, self.inventory_path)
            return
        if self.inventory_path.is_file() and file_sha256(self.inventory_path) == expected:
            self.current_inventory_path.unlink(missing_ok=True)
            return
        raise ValueError("追加后的源快照缺失或校验失败，已保留检查点供下次恢复")

    def _migrate_legacy_inventory(self):
        """旧版本没有源快照时，从 tar 内 JSON 恢复源路径顺序。"""
        print(f"[INFO] {self.split}: 首次升级，正在从旧分片建立源快照", flush=True)
        tmp_path = self.inventory_path.with_suffix(self.inventory_path.suffix + ".tmp")
        count = 0
        with open(tmp_path, "w", encoding="utf-8", newline="\n") as stream:
            header = {
                "format": INVENTORY_FORMAT,
                "version": STATE_VERSION,
                "signature": self.signature,
            }
            stream.write(json.dumps(header, ensure_ascii=False, separators=(",", ":")) + "\n")
            for shard in self.base_manifest["shards"]:
                tar_path = self.output_root / shard["path"]
                index = ShardIndex(self.output_root / shard["index"], shard["shard_id"])
                try:
                    with open(tar_path, "rb") as tar_stream:
                        for row_index in range(index.count):
                            record = index.record(row_index)
                            if record.sample_id != count:
                                raise ValueError("旧分片样本序号不连续，无法建立源快照")
                            tar_stream.seek(record.label_offset)
                            payload = tar_stream.read(record.label_size)
                            if len(payload) != record.label_size:
                                raise ValueError(f"旧分片标签读取不完整: {tar_path}")
                            label = json.loads(payload.decode("utf-8"))
                            relative_path = Path(label["source"])
                            if relative_path.is_absolute() or ".." in relative_path.parts:
                                raise ValueError(f"旧分片包含不安全的源路径: {relative_path}")
                            image_path = self.images_root / relative_path
                            label_path = self.labels_root / relative_path.with_suffix(".txt")
                            image_size, image_mtime = file_version(image_path)
                            label_size, label_mtime = file_version(label_path)
                            entry = [
                                relative_path.as_posix(),
                                image_size,
                                image_mtime,
                                label_size,
                                label_mtime,
                            ]
                            stream.write(
                                json.dumps(entry, ensure_ascii=False, separators=(",", ":")) + "\n"
                            )
                            count += 1
                finally:
                    index.close()
            stream.flush()
            os.fsync(stream.fileno())
        if count != self.base_manifest["total_samples"]:
            raise ValueError("旧分片源路径数量与 manifest 不一致")
        os.replace(tmp_path, self.inventory_path)

    @staticmethod
    def _import_inventory(db, table, entries):
        sql = f"INSERT INTO {table} VALUES (?, ?, ?, ?, ?, ?)"
        batch = []
        for sequence, entry in enumerate(entries):
            batch.append((*entry, sequence))
            if len(batch) >= 5000:
                db.executemany(sql, batch)
                batch.clear()
        if batch:
            db.executemany(sql, batch)

    def _diff_inventories(self):
        self.diff_path.unlink(missing_ok=True)
        db = sqlite3.connect(self.diff_path)
        try:
            db.execute("PRAGMA journal_mode=OFF")
            db.execute("PRAGMA synchronous=OFF")
            columns = (
                "path TEXT PRIMARY KEY, image_size INTEGER, image_mtime INTEGER, "
                "label_size INTEGER, label_mtime INTEGER, seq INTEGER"
            )
            db.execute(f"CREATE TABLE old ({columns}) WITHOUT ROWID")
            db.execute(f"CREATE TABLE new ({columns}) WITHOUT ROWID")
            self._import_inventory(db, "old", self._iter_inventory())
            self._import_inventory(
                db, "new", self._iter_inventory(self.current_inventory_path)
            )
            db.commit()

            deleted = db.execute(
                "SELECT o.path FROM old o LEFT JOIN new n ON n.path=o.path "
                "WHERE n.path IS NULL LIMIT 3"
            ).fetchall()
            if deleted:
                examples = "、".join(row[0] for row in deleted)
                raise IncrementalConflict(
                    f"检测到旧图片被删除，增量追加仅支持新增: {examples}。"
                    "如需删除，请使用新的输出目录重新打包"
                )
            changed = db.execute(
                "SELECT o.path FROM old o JOIN new n ON n.path=o.path WHERE "
                "o.image_size!=n.image_size OR o.image_mtime!=n.image_mtime OR "
                "o.label_size!=n.label_size OR o.label_mtime!=n.label_mtime LIMIT 3"
            ).fetchall()
            if changed:
                examples = "、".join(row[0] for row in changed)
                raise IncrementalConflict(
                    f"检测到旧图片或标签被修改，拒绝静默混用版本: {examples}。"
                    "如需修改，请使用新的输出目录重新打包"
                )

            where_added = "FROM new n LEFT JOIN old o ON o.path=n.path WHERE o.path IS NULL"
            append_count = db.execute(f"SELECT count(*) {where_added}").fetchone()[0]
            tmp_path = self.append_inventory_path.with_suffix(
                self.append_inventory_path.suffix + ".tmp"
            )
            with open(tmp_path, "w", encoding="utf-8", newline="\n") as stream:
                header = {
                    "format": INVENTORY_FORMAT,
                    "version": STATE_VERSION,
                    "signature": self.signature,
                }
                stream.write(json.dumps(header, ensure_ascii=False, separators=(",", ":")) + "\n")
                rows = db.execute(
                    "SELECT n.path,n.image_size,n.image_mtime,n.label_size,n.label_mtime,n.seq "
                    f"{where_added} ORDER BY n.seq"
                )
                for row in rows:
                    stream.write(
                        json.dumps(list(row[:5]), ensure_ascii=False, separators=(",", ":")) + "\n"
                    )
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(tmp_path, self.append_inventory_path)
            return append_count
        finally:
            db.close()

    def update(self):
        state = None
        if self.checkpoint_path.is_file():
            with open(self.checkpoint_path, "r", encoding="utf-8") as stream:
                state = json.load(stream)
            self._validate_checkpoint(state)
            complete = state["sample_id"] == state["total_samples"] and not state["current_shard"]
            if complete and self._manifest_from_state(state) == self.base_manifest:
                # manifest 已发布而源快照尚未切换时，只完成剩余发布步骤。
                self._publish_snapshot(state)
                self._cleanup_transients()
                print(f"[INFO] {self.split}: 已恢复上次增量发布", flush=True)
                state = None
            elif state["base_manifest"] != self.base_manifest:
                raise ValueError("当前 manifest 与追加检查点的起止状态均不一致")
            else:
                self._restore_state(state)
                print(
                    f"[INFO] {self.split}: 恢复增量追加，已提交 "
                    f"{self.sample_id - self.base_manifest['total_samples']}/{self.append_count} 张",
                    flush=True,
                )
                self.base_manifest = self._append(state.get("current_shard"))
                # 恢复期间可能又加入了图片，完成旧检查点后再检查一轮。

        if not self.inventory_path.is_file():
            self._migrate_legacy_inventory()
        for path in (self.current_inventory_path, self.append_inventory_path, self.diff_path):
            path.unlink(missing_ok=True)
        self._write_inventory(self.current_inventory_path)
        self.append_count = self._diff_inventories()
        if self.append_count == 0:
            os.replace(self.current_inventory_path, self.inventory_path)
            self.append_inventory_path.unlink(missing_ok=True)
            self.diff_path.unlink(missing_ok=True)
            return self.base_manifest

        self.total_samples = self.base_manifest["total_samples"] + self.append_count
        self.sample_id = self.base_manifest["next_sample_id"]
        self.class_counts = list(self.base_manifest["class_counts"])
        self.max_objects = self.base_manifest["max_objects_per_image"]
        self.shards = list(self.base_manifest["shards"])
        self.inventory_sha256 = file_sha256(self.current_inventory_path)
        self._save_checkpoint()
        return self._append(None)

    def _append(self, current):
        (self.output_root / "shards").mkdir(parents=True, exist_ok=True)
        (self.output_root / "index").mkdir(parents=True, exist_ok=True)
        base_total = self.base_manifest["total_samples"]
        writer = ShardWriter.resume(self.output_root, current) if current else None
        progress = TQDM(
            total=self.append_count,
            initial=self.sample_id - base_total,
            desc=f"{self.split} 增量追加",
            unit="张",
            mininterval=0.2,
        )
        try:
            for source_index, entry in enumerate(self._iter_inventory(self.append_inventory_path)):
                image_path, relative_path, label_path = self._verify_source(entry)
                processed = self.sample_id - base_total
                if source_index < processed:
                    continue
                if source_index != processed:
                    raise ValueError("新增文件清单与追加检查点位置不一致")

                image_bytes = image_path.read_bytes()
                array = np.frombuffer(image_bytes, dtype=np.uint8)
                image = cv2.imdecode(array, cv2.IMREAD_UNCHANGED)
                if image is None:
                    raise ValueError(f"图片解码失败: {image_path}")
                height, width = image.shape[:2]
                annotation, object_count, flags, class_ids = parse_yolo_label(
                    label_path, self.class_count
                )
                for class_id in class_ids:
                    self.class_counts[class_id] += 1
                self.max_objects = max(self.max_objects, object_count)

                if writer is None:
                    writer = ShardWriter(self.output_root, len(self.shards))
                writer.add(
                    self.sample_id,
                    relative_path,
                    image_bytes,
                    annotation,
                    width,
                    height,
                    CODEC_IDS[image_path.suffix.lower()],
                    flags,
                )
                self.sample_id += 1
                progress.update(1)

                if len(writer.rows) >= self.shard_size or writer.raw.tell() >= self.shard_bytes:
                    self.shards.append(writer.close())
                    writer = None
                    self._save_checkpoint()
                elif (self.sample_id - base_total) % self.checkpoint_interval == 0:
                    self._save_checkpoint(writer)

            if writer is not None:
                self.shards.append(writer.close())
                writer = None
            if self.sample_id != self.total_samples:
                raise ValueError(
                    f"新增清单不完整: 已处理 {self.sample_id - base_total}, "
                    f"共 {self.append_count}"
                )
            state = self._save_checkpoint()
        finally:
            progress.close()

        manifest = self._manifest_from_state(state)
        atomic_json(self.manifest_path, manifest)
        self._publish_snapshot(state)
        self._cleanup_transients()
        return manifest


def _write_packed_data_yaml(packed_data, names):
    """原子写入打包版 data.yaml，train/val 固定指向输出目录下的分片子目录。"""
    packed_yaml = {
        "train": "train",
        "val": "val",
        "names": names,
        "keyolo_format": DATASET_FORMAT,
    }
    tmp_path = packed_data.with_suffix(".yaml.tmp")
    YAML.save(tmp_path, packed_yaml)
    os.replace(tmp_path, packed_data)


def pack_dataset(
    data_path,
    output,
    shard_size=5000,
    shard_mb=1024,
    checkpoint_interval=100,
    resume=False,
):
    """打包数据集并返回打包版 data.yaml；也可由训练入口直接调用。"""
    if shard_size <= 0 or shard_mb <= 0 or checkpoint_interval <= 0:
        raise ValueError("shard-size、shard-mb 和 checkpoint-interval 必须大于 0")
    data_path = Path(data_path).resolve()
    output = Path(output).resolve()
    if output.exists() and not output.is_dir():
        raise NotADirectoryError(f"输出路径不是目录: {output}")
    if resume and not output.exists():
        raise FileNotFoundError(f"没有可续传的输出目录: {output}")
    if not resume and output.exists() and any(output.iterdir()):
        raise FileExistsError(f"非续传模式要求输出目录为空: {output}")
    output.mkdir(parents=True, exist_ok=True)

    packed_data = output / "data.yaml"
    if (
        resume
        and packed_data.is_file()
        and all((output / split / "manifest.json").is_file() for split in ("train", "val"))
    ):
        # 续传命中已完整发布的数据集时，也重写 data.yaml，避免旧版本残留错误路径。
        _, names = load_data_yaml(data_path)
        _write_packed_data_yaml(packed_data, names)
        print(f"[DONE] 数据集已经完整发布，无需续传: {packed_data}", flush=True)
        return packed_data

    try:
        splits, names = load_data_yaml(data_path)
        summaries = {}
        for split, images_root in splits.items():
            manifest_path = output / split / "manifest.json"
            if resume and manifest_path.is_file():
                with open(manifest_path, "r", encoding="utf-8") as stream:
                    summaries[split] = json.load(stream)
                print(f"[INFO] {split} 已完成，跳过该数据集分段", flush=True)
                continue

            print(f"[INFO] 开始打包 {split}: {images_root}", flush=True)
            packer = SplitPacker(
                split,
                images_root,
                output / split,
                len(names),
                shard_size,
                shard_mb << 20,
                checkpoint_interval,
            )
            summaries[split] = packer.pack()

        _write_packed_data_yaml(packed_data, names)

        for split, manifest in summaries.items():
            print(
                f"[DONE] {split}: {manifest['total_samples']} 张，"
                f"{manifest['shard_count']} 个分片",
                flush=True,
            )
        print(f"[DONE] 数据集已发布: {packed_data}", flush=True)
        return packed_data
    except KeyboardInterrupt:
        print(
            f"\n[INFO] 打包已中断，最近检查点已保留。下次训练将自动从检查点继续: {output}",
            file=sys.stderr,
            flush=True,
        )
        raise SystemExit(130)


def update_packed_dataset(
    data_path,
    output,
    shard_size=5000,
    shard_mb=1024,
    checkpoint_interval=100,
):
    """检查原始目录并自动追加新增图片；旧分片保持字节不变。"""
    if shard_size <= 0 or shard_mb <= 0 or checkpoint_interval <= 0:
        raise ValueError("shard-size、shard-mb 和 checkpoint-interval 必须大于 0")
    data_path = Path(data_path).resolve()
    output = Path(output).resolve()
    packed_data = output / "data.yaml"
    if not packed_data.is_file():
        raise FileNotFoundError(f"打包数据集不存在: {packed_data}")

    splits, names = load_data_yaml(data_path)
    packed_config = YAML.load(packed_data)
    if packed_config.get("names") != names:
        raise ValueError("原始 data.yaml 的类别名称或顺序与已打包数据集不一致")

    try:
        for split, images_root in splits.items():
            manifest_path = output / split / "manifest.json"
            if not manifest_path.is_file():
                raise FileNotFoundError(f"打包数据集缺少 manifest: {manifest_path}")
            with open(manifest_path, "r", encoding="utf-8") as stream:
                previous = json.load(stream)

            packer = IncrementalPacker(
                split,
                images_root,
                output / split,
                len(names),
                shard_size,
                shard_mb << 20,
                checkpoint_interval,
                previous,
            )
            manifest = packer.update()
            added = manifest["total_samples"] - previous["total_samples"]
            if added:
                print(
                    f"[DONE] {split}: 新增 {added} 张，总计 {manifest['total_samples']} 张，"
                    f"新增分片从 #{previous['shard_count']} 开始",
                    flush=True,
                )
            else:
                print(f"[INFO] {split}: 没有新增图片，沿用现有分片", flush=True)
        # 每次增量更新后重写 data.yaml，修正旧版本残留的 train/val 路径。
        _write_packed_data_yaml(packed_data, names)
        return packed_data
    except KeyboardInterrupt:
        print(
            f"\n[INFO] 增量追加已中断，检查点已保留。下次训练将自动继续: {output}",
            file=sys.stderr,
            flush=True,
        )
        raise SystemExit(130)


def rebuild_packed_dataset(
    data_path,
    output,
    shard_size=5000,
    shard_mb=1024,
    checkpoint_interval=100,
):
    """源图片/标签变更后自动整包重建。

    旧打包目录先整体重命名备份，重建成功后再删除；
    重建失败时清理半成品并还原旧目录，避免丢失可用数据。
    """
    data_path = Path(data_path).resolve()
    output = Path(output).resolve()
    backup = None
    if output.exists():
        if not output.is_dir():
            raise NotADirectoryError(f"输出路径不是目录: {output}")
        stamp = time.strftime("%Y%m%d_%H%M%S")
        backup = output.with_name(f"{output.name}.old.{stamp}")
        index = 1
        while backup.exists():
            backup = output.with_name(f"{output.name}.old.{stamp}_{index}")
            index += 1
        os.replace(output, backup)
        print(f"[INFO] 已备份旧打包目录: {backup}", flush=True)

    try:
        result = pack_dataset(
            data_path,
            output,
            shard_size=shard_size,
            shard_mb=shard_mb,
            checkpoint_interval=checkpoint_interval,
        )
    except BaseException:
        # 重建失败：删除半成品并还原旧目录，保证下次仍可用
        shutil.rmtree(output, ignore_errors=True)
        if backup is not None and backup.exists():
            os.replace(backup, output)
        raise

    if backup is not None:
        shutil.rmtree(backup, ignore_errors=True)
        print(f"[INFO] 旧打包目录已清理: {backup}", flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description="离线打包 YOLO 检测数据集")
    parser.add_argument("--data", type=Path, required=True, help="原始 YOLO data.yaml")
    parser.add_argument("--output", type=Path, required=True, help="输出目录")
    parser.add_argument("--shard-size", type=int, default=5000, help="每分片最大图片数")
    parser.add_argument("--shard-mb", type=int, default=1024, help="每分片目标最大 MiB 数")
    parser.add_argument(
        "--checkpoint-interval",
        type=int,
        default=100,
        help="每处理多少张图片保存一次检查点，默认 100",
    )
    parser.add_argument("--resume", action="store_true", help="从输出目录中的最近检查点继续打包")
    args = parser.parse_args()
    pack_dataset(
        args.data,
        args.output,
        shard_size=args.shard_size,
        shard_mb=args.shard_mb,
        checkpoint_interval=args.checkpoint_interval,
        resume=args.resume,
    )


if __name__ == "__main__":
    main()
