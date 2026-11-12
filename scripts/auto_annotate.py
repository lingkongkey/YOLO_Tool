# -*- coding: utf-8 -*-
"""
自动预标注工具
用一个预训练/已训练的 YOLO 模型对图片目录自动生成 YOLO 格式标签(.txt)，
之后用 labelImg 打开人工校对，大幅减少标注工作量。

用法:
    python auto_annotate.py --imgs ../datasets/my_dataset/images/train \
                            --weights ../models/yolov8n.pt \
                            --conf 0.5
生成的 .txt 放到 labels/train。
注意: 类别会按 weights 模型的类别输出，若你的数据集类别不同请人工修正或指定 --classes。
【修改说明】：labels目录下空txt会重新推理；只有存在且非空txt才跳过
"""
import argparse
import os
import sys

from ultralytics import YOLO
from PIL import Image


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--imgs", default=None, help="图片目录（与 --image 二选一）")
    ap.add_argument("--image", default=None, help="只处理单张图片（会覆盖已有标签）")
    ap.add_argument("--weights", required=True, help="模型权重 .pt")
    ap.add_argument("--conf", type=float, default=0.5)
    ap.add_argument("--imgsz", type=int, default=640)
    ap.add_argument("--classes", default=None,
                    help="只保留指定类别 id，逗号分隔，如 0,1")
    ap.add_argument("--save-img", action="store_true",
                    help="同时保存带框的预览图到 _preview 子目录")
    args = ap.parse_args()

    single_mode = bool(args.image)
    if single_mode:
        img_path = os.path.abspath(args.image)
        if not os.path.isfile(img_path):
            print(f"[ERROR] 图片不存在: {img_path}")
            sys.exit(1)
        img_dir = os.path.dirname(img_path)
        files = [os.path.basename(img_path)]
    else:
        if not args.imgs:
            print("[ERROR] 必须指定 --imgs 或 --image")
            sys.exit(1)
        img_dir = os.path.abspath(args.imgs)
        if not os.path.isdir(img_dir):
            print(f"[ERROR] 目录不存在: {img_dir}")
            sys.exit(1)
        exts = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}
        files = [f for f in os.listdir(img_dir)
                 if os.path.splitext(f)[1].lower() in exts]

    # 标签目录：images/train -> labels/train（与训练脚本、标注工具一致）
    parts = list(os.path.normpath(img_dir).split(os.sep))
    lbl_dir = None
    for i in range(len(parts) - 1, -1, -1):
        if parts[i].lower() == "images":
            parts[i] = "labels"
            lbl_dir = os.sep.join(parts)
            break
    if lbl_dir is None:
        lbl_dir = os.path.join(os.path.dirname(img_dir), "labels")
    os.makedirs(lbl_dir, exist_ok=True)

    # ========== 这里是修改后的判断逻辑 ==========
    if single_mode:
        # 单张模式：用户明确指定了图片，直接重新推理并覆盖旧标签
        todo = files
    else:
        todo = []
        for name in files:
            stem = os.path.splitext(name)[0]
            lbl_path = os.path.join(lbl_dir, stem + ".txt")
            img_side_txt = os.path.join(img_dir, stem + ".txt")

            # 如果图片同目录有txt，直接跳过（人工放在图片旁边的标注）
            if os.path.isfile(img_side_txt):
                continue
            # labels文件夹里面的txt存在，并且大小>0，才跳过；空文件（0字节）不跳过
            if os.path.isfile(lbl_path):
                file_size = os.stat(lbl_path).st_size
                if file_size > 0:
                    continue
            todo.append(name)
    # ===========================================

    print(f"[INFO] 共 {len(files)} 张图片，待预标注 {len(todo)} 张")
    if not todo:
        print("[DONE] 没有需要预标注的图片")
        return
    print(f"[INFO] 加载模型 {args.weights}")

    model = YOLO(args.weights)
    keep = None
    if args.classes:
        keep = set(int(c) for c in args.classes.split(","))

    preview_dir = os.path.join(img_dir, "_preview")
    if args.save_img:
        os.makedirs(preview_dir, exist_ok=True)

    for i, name in enumerate(todo, 1):
        path = os.path.join(img_dir, name)
        results = model.predict(path, conf=args.conf, imgsz=args.imgsz,
                                verbose=False)
        r = results[0]
        h, w = r.orig_shape
        lines = []
        for box in r.boxes:
            cls = int(box.cls[0])
            if keep is not None and cls not in keep:
                continue
            cx, cy, bw, bh = box.xywhn[0].tolist()
            lines.append(f"{cls} {cx:.6f} {cy:.6f} {bw:.6f} {bh:.6f}")
        txt_path = os.path.join(lbl_dir, os.path.splitext(name)[0] + ".txt")
        with open(txt_path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + ("\n" if lines else ""))
        if args.save_img:
            cvimg = r.plot()
            from PIL import Image
            Image.fromarray(cvimg[..., ::-1]).save(
                os.path.join(preview_dir, name))
        print(f"  [{i}/{len(todo)}] {name} -> {len(lines)} boxes")

    print(f"[DONE] 标签已生成在 {lbl_dir}，请用 labelImg 打开校对")


if __name__ == "__main__":
    main()
