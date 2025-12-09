import shutil
import os

repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
old_path = os.path.join(repo_root, "distributor", "firmware", "esp32c3supermini")
new_path = os.path.join(repo_root, "distributor", "firmware", "esp32c3")

if os.path.exists(old_path):
    if os.path.exists(new_path):
        print(f"Target {new_path} already exists. Removing old path.")
        shutil.rmtree(old_path)
    else:
        print(f"Renaming {old_path} to {new_path}")
        shutil.move(old_path, new_path)
else:
    print(f"Old path {old_path} not found. Nothing to do.")
