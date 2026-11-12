# -*- coding: utf-8 -*-
"""Keyolo 未压缩 tar 分片与定长二进制索引的公共读写实现。"""

import bisect
import hashlib
import json
import mmap
import os
import struct
import zlib
from collections import OrderedDict
from dataclasses import dataclass
from pathlib import Path

DATASET_FORMAT = "keyolo-webdataset"
INDEX_MAGIC = b"KYLOIDX1"
INDEX_VERSION = 1
INDEX_HEADER = struct.Struct("<8sIIIIQQQQ8s")
INDEX_RECORD = struct.Struct("<QQIQIQIIIIBBH")
INDEX_HEADER_SIZE = INDEX_HEADER.size
INDEX_RECORD_SIZE = INDEX_RECORD.size

CODEC_IDS = {
    ".jpg": 1,
    ".jpeg": 1,
    ".png": 2,
    ".bmp": 3,
    ".webp": 4,
    ".tif": 5,
    ".tiff": 5,
}


@dataclass(frozen=True)
class IndexRecord:
    shard_id: int
    row_index: int
    sample_id: int
    image_offset: int
    image_size: int
    label_offset: int
    label_size: int
    annotation_offset: int
    annotation: bytes
    width: int
    height: int
    crc32: int
    codec: int
    flags: int


def file_sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(8 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def write_index(path, shard_id, rows):
    """原子写入一个分片索引；标注字节紧跟在定长记录区之后。"""
    path = Path(path)
    count = len(rows)
    if count <= 0:
        raise ValueError("不能写入空分片索引")

    records_offset = INDEX_HEADER_SIZE
    annotations_offset = records_offset + count * INDEX_RECORD_SIZE
    next_annotation_offset = annotations_offset
    encoded_rows = []

    for row in rows:
        annotation = row["annotation"]
        encoded_rows.append((
            row["sample_id"],
            row["image_offset"],
            row["image_size"],
            row["label_offset"],
            row["label_size"],
            next_annotation_offset,
            len(annotation),
            row["width"],
            row["height"],
            row["crc32"],
            row["codec"],
            row["flags"],
            0,
        ))
        next_annotation_offset += len(annotation)

    header = INDEX_HEADER.pack(
        INDEX_MAGIC,
        INDEX_VERSION,
        INDEX_HEADER_SIZE,
        INDEX_RECORD_SIZE,
        shard_id,
        count,
        records_offset,
        annotations_offset,
        next_annotation_offset,
        b"\0" * 8,
    )

    tmp_path = path.with_suffix(path.suffix + ".tmp")
    with open(tmp_path, "wb") as stream:
        stream.write(header)
        for values in encoded_rows:
            stream.write(INDEX_RECORD.pack(*values))
        for row in rows:
            stream.write(row["annotation"])
        stream.flush()
        os.fsync(stream.fileno())

    if tmp_path.stat().st_size != next_annotation_offset:
        raise IOError(f"索引长度不一致: {tmp_path}")
    os.replace(tmp_path, path)
    return file_sha256(path)


class ShardIndex:
    """单个分片的只读 mmap 索引。"""

    def __init__(self, path, expected_shard_id):
        self.path = Path(path)
        self._file = open(self.path, "rb")
        try:
            header_bytes = self._file.read(INDEX_HEADER_SIZE)
            if len(header_bytes) != INDEX_HEADER_SIZE:
                raise ValueError("索引头不完整")
            values = INDEX_HEADER.unpack(header_bytes)
            magic = values[0]
            version, header_size, record_size, self.shard_id = values[1:5]
            self.count, self.records_offset, self.annotations_offset, file_size = values[5:9]

            if magic != INDEX_MAGIC or version != INDEX_VERSION:
                raise ValueError("索引格式或版本不受支持")
            if self.shard_id != expected_shard_id:
                raise ValueError(f"索引分片号不一致: {self.shard_id} != {expected_shard_id}")
            if header_size != INDEX_HEADER_SIZE or record_size != INDEX_RECORD_SIZE:
                raise ValueError("索引记录尺寸不匹配")
            if self.count <= 0 or self.records_offset != INDEX_HEADER_SIZE:
                raise ValueError("索引记录区无效")
            if self.annotations_offset != self.records_offset + self.count * INDEX_RECORD_SIZE:
                raise ValueError("索引标注区偏移无效")
            if file_size != self.path.stat().st_size:
                raise ValueError("索引文件被截断或包含额外数据")

            self._mmap = mmap.mmap(self._file.fileno(), 0, access=mmap.ACCESS_READ)
            self.file_size = file_size
        except Exception:
            self._file.close()
            raise

    def record(self, row_index):
        if row_index < 0:
            row_index += self.count
        if row_index < 0 or row_index >= self.count:
            raise IndexError(row_index)

        offset = self.records_offset + row_index * INDEX_RECORD_SIZE
        values = INDEX_RECORD.unpack_from(self._mmap, offset)
        annotation_offset = values[5]
        annotation_size = values[6]
        annotation_end = annotation_offset + annotation_size
        if annotation_offset < self.annotations_offset or annotation_end > self.file_size:
            raise ValueError(f"样本标注偏移越界: shard={self.shard_id}, row={row_index}")

        return IndexRecord(
            shard_id=self.shard_id,
            row_index=row_index,
            sample_id=values[0],
            image_offset=values[1],
            image_size=values[2],
            label_offset=values[3],
            label_size=values[4],
            annotation_offset=annotation_offset,
            annotation=self._mmap[annotation_offset:annotation_end],
            width=values[7],
            height=values[8],
            crc32=values[9],
            codec=values[10],
            flags=values[11],
        )

    def close(self):
        if self._mmap is not None:
            self._mmap.close()
            self._mmap = None
        if self._file is not None:
            self._file.close()
            self._file = None


class TarShardStore:
    """全局序号到 shard/offset 的映射；文件和 mmap 均在 worker 内惰性打开。"""

    def __init__(self, root, cache_size=4, verify_crc=False):
        root = Path(root)
        self.manifest_path = root if root.is_file() else root / "manifest.json"
        self.root = self.manifest_path.parent
        self.cache_size = max(1, int(cache_size))
        self.verify_crc = verify_crc

        with open(self.manifest_path, "r", encoding="utf-8") as stream:
            self.manifest = json.load(stream)
        if self.manifest.get("format") != DATASET_FORMAT:
            raise ValueError(f"不是 {DATASET_FORMAT} 数据集: {self.manifest_path}")
        if self.manifest.get("compression") != "none":
            raise ValueError("随机偏移读取仅支持未压缩 tar 分片")
        if self.manifest.get("total_samples") != self.manifest.get("active_samples"):
            raise ValueError("当前训练读取器要求不可变快照，请先压缩整理逻辑删除的数据")

        self.shards = self.manifest["shards"]
        if not self.shards:
            raise ValueError("数据集没有分片")
        self.ends = []
        total = 0
        for expected_id, shard in enumerate(self.shards):
            if shard["shard_id"] != expected_id or shard["sample_count"] <= 0:
                raise ValueError("manifest 分片编号或样本数无效")
            total += shard["sample_count"]
            self.ends.append(total)
        if total != self.manifest["total_samples"]:
            raise ValueError("manifest 总样本数与分片统计不一致")

        self._index_cache = OrderedDict()
        self._file_cache = OrderedDict()

    def __len__(self):
        return self.manifest["total_samples"]

    @property
    def class_counts(self):
        return self.manifest.get("class_counts", [])

    @property
    def max_objects_per_image(self):
        return self.manifest.get("max_objects_per_image", 0)

    def _resolve(self, relative_path):
        relative = Path(relative_path)
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError(f"数据集中的路径必须是安全的相对路径: {relative_path}")
        return self.root / relative

    def _get_index(self, shard_id):
        index = self._index_cache.pop(shard_id, None)
        if index is None:
            shard = self.shards[shard_id]
            index = ShardIndex(self._resolve(shard["index"]), shard_id)
        self._index_cache[shard_id] = index
        while len(self._index_cache) > self.cache_size:
            _, old = self._index_cache.popitem(last=False)
            old.close()
        return index

    def _get_file(self, shard_id):
        stream = self._file_cache.pop(shard_id, None)
        if stream is None:
            stream = open(self._resolve(self.shards[shard_id]["path"]), "rb", buffering=0)
        self._file_cache[shard_id] = stream
        while len(self._file_cache) > self.cache_size:
            _, old = self._file_cache.popitem(last=False)
            old.close()
        return stream

    def get_record(self, index):
        if index < 0:
            index += len(self)
        shard_id = bisect.bisect_right(self.ends, index)
        if shard_id >= len(self.shards) or index < 0:
            raise IndexError(index)
        shard_start = 0 if shard_id == 0 else self.ends[shard_id - 1]
        return self._get_index(shard_id).record(index - shard_start)

    def read_image(self, record):
        stream = self._get_file(record.shard_id)
        stream.seek(record.image_offset)
        data = stream.read(record.image_size)
        if len(data) != record.image_size:
            raise IOError(f"图片读取长度不足: sample={record.sample_id}")
        if self.verify_crc and zlib.crc32(data) != record.crc32:
            raise IOError(f"图片 CRC32 校验失败: sample={record.sample_id}")
        return data

    def virtual_path(self, record):
        shard_path = self._resolve(self.shards[record.shard_id]["path"])
        return f"{shard_path}#{record.sample_id:016x}"

    def close(self):
        for index in self._index_cache.values():
            index.close()
        for stream in self._file_cache.values():
            stream.close()
        self._index_cache.clear()
        self._file_cache.clear()

    def __getstate__(self):
        state = self.__dict__.copy()
        state["_index_cache"] = OrderedDict()
        state["_file_cache"] = OrderedDict()
        return state

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass
