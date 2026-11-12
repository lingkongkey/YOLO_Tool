# -*- coding: utf-8 -*-
"""
YOLO 训练脚本（CPU 版）
用法:
    python train.py --data ../datasets/my_dataset/data.yaml --weights ../models/yolov8n.pt \
                    --epochs 100 --imgsz 640 --batch 4
    python train.py --data xxx.yaml --weights yolov8s.pt --device cpu
训练产出在 ../output/runs/detect/ 下
"""
import argparse
import os
import random
import shutil
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from ultralytics import YOLO

# 支持的图片后缀（与 ultralytics 的图片格式一致）
IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp", ".webp", ".tif", ".tiff",
                  ".jp2", ".dng", ".mpo", ".heic", ".heif", ".avif"}


def read_dataset_paths(data_path):
    """不依赖 PyYAML，简易读取 data.yaml 里的 path/train/val 三行。"""
    info = {}
    with open(data_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line or ":" not in line:
                continue
            key, value = line.split(":", 1)
            key = key.strip()
            value = value.strip().strip("'\"")
            if key in ("path", "train", "val") and value:
                info[key] = value
    return info


def is_packed_dataset(data_path):
    """仅检查 manifest 是否存在，不枚举图片目录。"""
    try:
        data = read_dataset_paths(data_path)
        root = Path(data.get("path") or Path(data_path).parent)
        if not root.is_absolute():
            root = (Path(data_path).parent / root).resolve()
        train_dir = (root / str(data.get("train", "images/train"))).resolve()
        val_dir = (root / str(data.get("val", "images/val"))).resolve()
        return (train_dir / "manifest.json").is_file() and (val_dir / "manifest.json").is_file()
    except Exception:
        return False


def is_packed_resume(weights_path):
    """从 Ultralytics args.yaml 恢复数据集类型，避免重新加载 checkpoint 判断。"""
    args_path = Path(weights_path).parent.parent / "args.yaml"
    if not args_path.is_file():
        return False
    try:
        from ultralytics.utils import YAML
        train_args = YAML.load(args_path)
        data_path = train_args.get("data")
        return bool(data_path) and is_packed_dataset(os.path.abspath(str(data_path)))
    except Exception:
        return False


def _labels_dir_for(img_dir):
    """images/train -> labels/train"""
    parts = list(Path(img_dir).parts)
    for i in range(len(parts) - 1, -1, -1):
        if parts[i].lower() == "images":
            parts[i] = "labels"
            return Path(*parts)
    return Path(img_dir).parent / "labels"


def ensure_val_split(data_path):
    """验证集为空时，从训练集切约 10%（至少 1 张）到 val，避免训练直接失败。

    采用移动而不是复制，避免同一张图同时出现在 train 和 val 造成验证指标虚高。
    返回切分张数（0 表示没动）。
    """
    try:
        data = read_dataset_paths(data_path)
    except Exception as e:
        print(f"[WARN] data.yaml 读取失败，跳过 val 自动切分: {e}")
        return 0

    if not data:
        return 0

    root = Path(data.get("path") or Path(data_path).parent)
    if not root.is_absolute():
        root = (Path(data_path).parent / root).resolve()

    train_dir = (root / str(data.get("train", "images/train"))).resolve()
    val_dir = (root / str(data.get("val", "images/val"))).resolve()

    if not train_dir.is_dir():
        return 0

    val_imgs = [p for p in val_dir.rglob("*") if p.suffix.lower() in IMAGE_SUFFIXES] if val_dir.is_dir() else []
    if val_imgs:
        return 0

    train_imgs = [p for p in train_dir.rglob("*") if p.suffix.lower() in IMAGE_SUFFIXES]
    if not train_imgs:
        return 0

    n_val = max(1, int(round(len(train_imgs) * 0.1)))
    random.seed(0)
    picked = random.sample(train_imgs, n_val)

    train_lbl = _labels_dir_for(train_dir)
    val_lbl = _labels_dir_for(val_dir)
    val_dir.mkdir(parents=True, exist_ok=True)
    val_lbl.mkdir(parents=True, exist_ok=True)

    moved = 0
    for img in picked:
        dst_img = val_dir / img.name
        try:
            shutil.move(str(img), str(dst_img))
        except Exception as e:
            print(f"[WARN] 移动失败，跳过: {img} -> {e}")
            continue

        src_lbl = train_lbl / (img.stem + ".txt")
        if src_lbl.is_file():
            try:
                shutil.move(str(src_lbl), str(val_lbl / src_lbl.name))
            except Exception as e:
                print(f"[WARN] 移动标签失败: {src_lbl} -> {e}")
        moved += 1

    # 训练/验证缓存里还是旧文件列表，删掉让 ultralytics 重新扫描
    for cache in ("train.cache", "val.cache"):
        for d in (train_lbl, val_lbl):
            p = d / cache
            if p.is_file():
                try:
                    p.unlink()
                except Exception:
                    pass

    print(f"[INFO] val 为空，已从 train 切分 {moved} 张图片（含标签）到 {val_dir}")
    return moved


def is_packed_output(data_path):
    """按目录结构判断 data.yaml 是否位于已打包输出目录（不依赖 data.yaml 内的 train/val 字段）。"""
    root = Path(data_path).resolve().parent
    return (root / "train" / "manifest.json").is_file() and (root / "val" / "manifest.json").is_file()


def auto_pack_dataset(data_path):
    """训练前自动打包、增量追加新增图片，并恢复未完成的检查点。"""
    data_path = Path(data_path).resolve()
    # 情况一：传入的已经是打包数据集，只需确保 data.yaml 的 train/val 路径指向分片目录。
    if is_packed_output(data_path):
        from pack_dataset import _write_packed_data_yaml
        from ultralytics.utils import YAML

        config = YAML.load(data_path)
        names = config.get("names")
        if isinstance(names, dict):
            names = [names.get(i, names.get(str(i))) for i in range(len(names))]
        _write_packed_data_yaml(data_path, names)
        print(f"[INFO] 已校验打包数据路径: {data_path}", flush=True)
        return str(data_path)

    # 情况二：传入原始散图 data.yaml，在同级 packed 目录下打包或增量追加。
    packed_root = data_path.parent / "packed"
    packed_data = packed_root / "data.yaml"
    if is_packed_output(packed_data):
        from pack_dataset import (
            IncrementalConflict,
            rebuild_packed_dataset,
            update_packed_dataset,
        )

        print(f"[INFO] 检查新增图片并自动增量追加: {packed_root}", flush=True)
        try:
            result = update_packed_dataset(data_path, packed_root)
            return str(result)
        except IncrementalConflict as exc:
            # 源图片或标签被修改/删除，增量追加无法保证一致性，自动整包重建
            print(f"[WARN] {exc}", flush=True)
            print(f"[INFO] 检测到源数据变更，自动重新打包: {packed_root}", flush=True)
            result = rebuild_packed_dataset(data_path, packed_root)
            return str(result)

    ensure_val_split(data_path)
    from pack_dataset import pack_dataset

    resume = packed_root.exists()
    if resume:
        print(f"[INFO] 检测到未完成的打包目录，将自动续传: {packed_root}", flush=True)
    else:
        print(f"[INFO] 首次训练需要打包，输出目录: {packed_root}", flush=True)
    result = pack_dataset(data_path, packed_root, resume=resume)
    return str(result)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default=None, help="数据集 data.yaml 路径（续训时可省略，从 checkpoint 恢复）")
    ap.add_argument("--weights", default="yolo11n.pt",
                    help="预训练权重，可填 yolo11n.pt / yolo11s.pt / yolov8n.pt 或本地路径")
    ap.add_argument("--epochs", type=int, default=100)
    ap.add_argument("--imgsz", type=int, default=640)
    ap.add_argument("--batch", type=int, default=4, help="CPU 建议 2-8")
    ap.add_argument("--device", default="cpu")
    ap.add_argument("--project", default="output/runs/detect")
    ap.add_argument("--name", default="train")
    ap.add_argument("--workers", type=int, default=0,
                    help="Windows CPU 下建议 0，避免多进程问题")
    ap.add_argument("--patience", type=int, default=30)
    ap.add_argument("--optimizer", default="auto")
    ap.add_argument("--lr0", type=float, default=None)
    ap.add_argument("--resume", action="store_true", help="断点续训（从 --weights 指定的 checkpoint 恢复训练）")
    ap.add_argument("--no-auto-pack", action="store_true", help="不自动打包，直接使用原始散图训练")
    args = ap.parse_args()

    # === 断点续训 ===
    # 必须把 weights 路径作为 resume 字符串传入；若传 resume=True，Ultralytics
    # 会调用 get_latest_run() 找到最近一次训练的 last.pt，忽略用户指定的 --weights。
    if args.resume:
        if not os.path.isfile(args.weights):
            print(f"[ERROR] resume 权重不存在: {args.weights}")
            sys.exit(1)
        # GPU 检查（续训时也要做，因为 checkpoint 里存的 device 可能和当前机器不匹配）
        if args.device not in ("cpu", ""):
            import torch
            if not torch.cuda.is_available():
                print("[ERROR] 选择了 GPU 训练，但 CUDA 不可用。")
                sys.exit(2)

        if is_packed_resume(args.weights):
            from tar_dataset import TarDetectionTrainer
            print(f"[INFO] tar 分片断点续训: {args.weights}")
            trainer = TarDetectionTrainer(overrides={"model": args.weights, "resume": args.weights})
            try:
                trainer.train()
            except AssertionError as exc:
                if "nothing to resume" in str(exc):
                    print(f"[INFO] 该权重已完成全部 epoch，无需续训: {args.weights}")
                    print("       如需继续训练，请取消“断点续训”并使用该权重作为预训练权重重新开始。")
                    return
                raise
            print("[DONE] 训练完成")
            return

        print(f"[INFO] 断点续训: {args.weights}")
        model = YOLO(args.weights)
        try:
            model.train(resume=args.weights)
        except AssertionError as exc:
            if "nothing to resume" in str(exc):
                print(f"[INFO] 该权重已完成全部 epoch，无需续训: {args.weights}")
                print("       如需继续训练，请取消“断点续训”并使用该权重作为预训练权重重新开始。")
                return
            raise
        print("[DONE] 训练完成")
        return

    # === 从头训练 / 微调 ===
    if not args.data:
        print("[ERROR] 必须提供 --data (数据集 data.yaml)")
        sys.exit(1)

    # GPU 训练前置检查：选了 CUDA 设备但 CUDA 不可用时直接报错，避免白跑
    if args.device not in ("cpu", ""):
        import torch
        if not torch.cuda.is_available():
            print("[ERROR] 选择了 GPU 训练，但 CUDA 不可用：请确认机器有 N 卡、"
                  "驱动已安装，且当前 python 环境的 torch 是 CUDA 版（非 +cpu 版）。")
            sys.exit(2)
        print(f"[INFO] GPU 训练: {torch.cuda.get_device_name(0)}")

    # 数据路径转绝对，避免工作目录影响
    data_path = os.path.abspath(args.data)
    if not os.path.isfile(data_path):
        print(f"[ERROR] data.yaml 不存在: {data_path}")
        sys.exit(1)

    packed = is_packed_dataset(data_path)
    if not args.no_auto_pack:
        # 无论原始散图还是已打包数据，都走 auto_pack：前者打包/增量追加，后者修正 data.yaml 路径。
        data_path = auto_pack_dataset(data_path)
        packed = True
    # 只有禁用自动打包时才沿用原散图训练；tar 数据集不再扫描或切分原始目录
    if not packed:
        ensure_val_split(data_path)

    print(f"[INFO] 加载模型: {args.weights}")
    if not packed:
        model = YOLO(args.weights)

    print(f"[INFO] 开始训练... data={data_path} epochs={args.epochs} "
          f"imgsz={args.imgsz} batch={args.batch} device={args.device}")

    train_kwargs = dict(
        data=data_path,
        epochs=args.epochs,
        imgsz=args.imgsz,
        batch=args.batch,
        device=args.device,
        project=os.path.abspath(args.project),
        name=args.name,
        workers=args.workers,
        patience=args.patience,
        optimizer=args.optimizer,
        pretrained=True,
        verbose=True,
    )
    if args.lr0 is not None:
        train_kwargs["lr0"] = args.lr0
    if args.resume:
        train_kwargs["resume"] = True

    if packed:
        from tar_dataset import TarDetectionTrainer
        trainer = TarDetectionTrainer(overrides={"model": args.weights, **train_kwargs})
        trainer.train()
        print("[DONE] tar 分片训练完成")
        print(f"  最佳权重: {Path(trainer.best)}")
        return

    results = model.train(**train_kwargs)
    print("[DONE] 训练完成")
    print(f"  最佳权重: {results.save_dir / 'weights' / 'best.pt'}")


if __name__ == "__main__":
    main()
