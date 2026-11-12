# -*- coding: utf-8 -*-
"""Ultralytics 检测训练器的 tar 分片数据集适配器。

训练期间不会枚举原始图片目录；标签来自 mmap 索引，图片按 shard+offset 读取。
"""

import math
import sys
from collections import OrderedDict
from copy import copy
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import cv2
import numpy as np
from ultralytics.data.dataset import YOLODataset
from ultralytics.engine.trainer import BaseTrainer
from ultralytics.models.yolo.detect.train import DetectionTrainer
from ultralytics.utils import LOGGER, colorstr
from ultralytics.utils.plotting import plot_labels
from ultralytics.utils.torch_utils import unwrap_model

from keyolo_tar import TarShardStore


def parse_annotation(annotation):
    if not annotation.strip():
        return np.empty((0, 5), dtype=np.float32)
    rows = [[float(value) for value in line.split()] for line in annotation.splitlines() if line.strip()]
    array = np.asarray(rows, dtype=np.float32)
    if array.ndim != 2 or array.shape[1] != 5:
        raise ValueError("索引中的 YOLO 标注必须为五列")
    return array


class LazyYoloLabels:
    """按需把索引记录转换为 Ultralytics label，避免创建百万个常驻 dict。"""

    def __init__(self, store, single_cls=False, include_class=None):
        self.store = store
        self.single_cls = single_cls
        self.include_class = include_class

    def __len__(self):
        return len(self.store)

    def __iter__(self):
        for index in range(len(self)):
            yield self[index]

    def __getitem__(self, index):
        if isinstance(index, slice):
            return [self[i] for i in range(*index.indices(len(self)))]
        record = self.store.get_record(index)
        values = parse_annotation(record.annotation.decode("utf-8"))

        if self.include_class is not None:
            include = np.asarray(self.include_class).reshape(1, -1)
            values = values[(values[:, 0:1] == include).any(1)]
        if self.single_cls:
            values[:, 0] = 0

        return {
            "im_file": self.store.virtual_path(record),
            "shape": (record.height, record.width),
            "cls": values[:, 0:1],
            "bboxes": values[:, 1:5],
            "segments": [],
            "keypoints": None,
            "normalized": True,
            "bbox_format": "xywh",
        }


class VirtualImageFiles:
    """仅为 Ultralytics 日志和绘图提供惰性虚拟路径，不指向散图。"""

    def __init__(self, store):
        self.store = store

    def __len__(self):
        return len(self.store)

    def __iter__(self):
        for index in range(len(self)):
            yield self[index]

    def __getitem__(self, index):
        if isinstance(index, slice):
            return [self[i] for i in range(*index.indices(len(self)))]
        return self.store.virtual_path(self.store.get_record(index))


class TarYoloDataset(YOLODataset):
    """兼容 YOLODataset 变换管线的惰性 tar 检测数据集。"""

    def __init__(
        self,
        img_path,
        imgsz=640,
        cache=False,
        augment=True,
        hyp=None,
        prefix="",
        rect=False,
        batch_size=16,
        stride=32,
        pad=0.5,
        single_cls=False,
        classes=None,
        fraction=1.0,
        data=None,
        task="detect",
        index_cache_size=4,
        verify_crc=False,
        **kwargs,
    ):
        if task != "detect":
            raise ValueError("当前 tar 加载器仅支持 YOLO detect 数据集")
        if cache:
            raise ValueError("tar 数据集暂不支持 Ultralytics cache=ram/disk，请直接使用分片与索引缓存")
        if fraction not in (None, 1, 1.0):
            raise ValueError("tar 数据集暂不支持 fraction 子集训练")
        if rect:
            raise ValueError("tar 数据集暂不支持 rect；训练器会使用普通 LetterBox")

        self.use_segments = False
        self.use_keypoints = False
        self.use_obb = False
        self.data = data or {}
        self.img_path = img_path
        self.imgsz = imgsz
        self.augment = augment
        self.single_cls = single_cls
        self.prefix = prefix
        self.fraction = 1.0
        self.channels = self.data.get("channels", 3)
        self.cv2_flag = {1: cv2.IMREAD_GRAYSCALE, 3: cv2.IMREAD_COLOR}.get(
            self.channels, cv2.IMREAD_UNCHANGED
        )

        self.store = TarShardStore(img_path, cache_size=index_cache_size, verify_crc=verify_crc)
        self.labels = LazyYoloLabels(self.store, single_cls=single_cls, include_class=classes)
        self.im_files = VirtualImageFiles(self.store)
        self.ni = len(self.store)
        self.rect = False
        self.batch_size = batch_size or 16
        self.stride = stride
        self.pad = pad
        self.cache = None

        self.buffer = []
        self.max_buffer_length = min(self.ni, self.batch_size * 8, 1000) if augment else 0
        self._image_buffer = OrderedDict()
        self.transforms = self.build_transforms(hyp=copy(hyp))

        LOGGER.info(
            f"{prefix}tar 索引加载完成: {self.ni} images, "
            f"{len(self.store.shards)} shards, 不扫描原始图片目录"
        )

    def load_image(self, i, rect_mode=True, resize_short=False):
        cached = self._image_buffer.get(i)
        if cached is not None:
            return cached

        record = self.store.get_record(i)
        data = self.store.read_image(record)
        image = cv2.imdecode(np.frombuffer(data, dtype=np.uint8), self.cv2_flag)
        if image is None:
            raise FileNotFoundError(f"tar 图片解码失败: sample={record.sample_id}")

        h0, w0 = image.shape[:2]
        if (h0, w0) != (record.height, record.width):
            raise ValueError(
                f"图片宽高与索引不一致: sample={record.sample_id}, "
                f"decoded={(h0, w0)}, index={(record.height, record.width)}"
            )

        shape = tuple(self.imgsz) if isinstance(self.imgsz, (tuple, list)) else (self.imgsz, self.imgsz)
        imgsz = max(shape)
        if rect_mode:
            if resize_short:
                ratio = imgsz / min(h0, w0)
                if ratio != 1:
                    w, h = (math.ceil(w0 * ratio), imgsz) if h0 < w0 else (imgsz, math.ceil(h0 * ratio))
                    image = cv2.resize(image, (w, h), interpolation=cv2.INTER_LINEAR)
            else:
                ratio = imgsz / max(h0, w0)
                if ratio != 1:
                    w, h = (min(math.ceil(w0 * ratio), imgsz), min(math.ceil(h0 * ratio), imgsz))
                    image = cv2.resize(image, (w, h), interpolation=cv2.INTER_LINEAR)
        elif (h0, w0) != shape:
            image = cv2.resize(image, shape[::-1], interpolation=cv2.INTER_LINEAR)

        if image.ndim == 2:
            image = image[..., None]
        result = (image, (h0, w0), image.shape[:2])

        if self.augment:
            self._image_buffer[i] = result
            self.buffer.append(i)
            if 1 < len(self.buffer) >= self.max_buffer_length:
                old = self.buffer.pop(0)
                self._image_buffer.pop(old, None)
        return result

    def close(self):
        self._image_buffer.clear()
        self.buffer.clear()
        self.store.close()


class TarDetectionTrainer(DetectionTrainer):
    """使用 tar 数据集的检测训练器，保留原生增强、验证、DDP 和 resume。"""

    def build_dataset(self, img_path, mode="train", batch=None):
        stride = max(int(unwrap_model(self.model).stride.max()), 32)
        if self.args.rect:
            LOGGER.warning("tar 数据集不支持 rect，已自动关闭")
        return TarYoloDataset(
            img_path=img_path,
            imgsz=self.args.imgsz,
            batch_size=batch,
            augment=mode == "train",
            hyp=self.args,
            rect=False,
            cache=False,
            single_cls=self.args.single_cls or False,
            stride=stride,
            pad=0.0 if mode == "train" else 0.5,
            prefix=colorstr(f"{mode}: "),
            task="detect",
            classes=self.args.classes,
            data=self.data,
            fraction=1.0,
        )

    def _build_train_pipeline(self):
        # 跳过 DetectionTrainer 中遍历全部 labels 的统计，改用打包时预计算结果
        BaseTrainer._build_train_pipeline(self)
        observed = max(
            self.train_loader.dataset.store.max_objects_per_image,
            self.test_loader.dataset.store.max_objects_per_image,
        )
        if observed > self.args.max_det:
            LOGGER.warning(f"tar 数据集中单图最多 {observed} 个目标，max_det 自动调整为 {observed}")
            self.args.max_det = observed
        unwrap_model(self.model).set_head_attr(max_det=self.args.max_det)

    def get_class_counts(self):
        counts = np.asarray(
            self.train_loader.dataset.store.class_counts,
            dtype=np.float32,
        )
        if counts.size != self.data["nc"]:
            raise ValueError("manifest 类别统计与 data.yaml 类别数不一致")
        if self.args.single_cls:
            return np.asarray([counts.sum()], dtype=np.float32)
        if self.args.classes is not None:
            filtered = np.zeros_like(counts)
            filtered[self.args.classes] = counts[self.args.classes]
            return filtered
        return counts

    def plot_training_labels(self):
        # 大数据集只抽样绘图，避免为了 labels.jpg 遍历全部索引
        dataset = self.train_loader.dataset
        sample_count = min(len(dataset), 20000)
        indices = np.linspace(0, len(dataset) - 1, sample_count, dtype=np.int64)
        labels = [dataset.labels[int(index)] for index in indices]
        boxes = np.concatenate([label["bboxes"] for label in labels], axis=0)
        classes = np.concatenate([label["cls"] for label in labels], axis=0)
        plot_labels(
            boxes,
            classes.squeeze(),
            names=self.data["names"],
            save_dir=self.save_dir,
            on_plot=self.on_plot,
        )
