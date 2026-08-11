import json
import os
import numpy as np
import torch

def mkdir_if_not_exist(path):
    if not os.path.exists(path):
        os.makedirs(path)

def normalize(x, x_min, x_max):
    return (x - x_min) / (x_max - x_min)

def denormalize(x, x_min, x_max):
    return x * (x_max - x_min) + x_min

def save_json(data, save_path):
    with open(save_path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)

def load_json(load_path):
    if not os.path.exists(load_path):
        return None
    with open(load_path, "r", encoding="utf-8") as f:
        return json.load(f)

def to_tensor(arr, device):
    return torch.from_numpy(arr).float().to(device)
