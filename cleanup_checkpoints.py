import os, shutil, time

ckpt_dir = r'G:\gd-ml-bot\checkpoints'

auto_ckpts = []
for name in os.listdir(ckpt_dir):
    full = os.path.join(ckpt_dir, name)
    if not os.path.isdir(full):
        continue
    if not name.startswith('auto_'):
        continue
    policy = os.path.join(full, 'policy.bin')
    if not os.path.exists(policy):
        continue
    mtime = os.path.getmtime(full)
    auto_ckpts.append((mtime, name, full))

auto_ckpts.sort(reverse=True)
print("Found " + str(len(auto_ckpts)) + " auto checkpoints")

print("Keeping 5 most recent:")
for i, (mtime, name, path) in enumerate(auto_ckpts[:5]):
    ts = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(mtime))
    print("  [" + str(i+1) + "] " + name + "  (" + ts + ")")

to_delete = auto_ckpts[5:]
print("Deleting " + str(len(to_delete)) + " old checkpoints...")
for mtime, name, path in to_delete:
    shutil.rmtree(path, ignore_errors=True)
    print("  Deleted: " + name)

print("Done! Checkpoints folder is clean.")
