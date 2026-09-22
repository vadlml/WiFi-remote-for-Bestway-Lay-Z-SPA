Import("env")
import os
import sys

# "tools" alone can collide with other packages on sys.path inside scons
sys.path.insert(0, os.path.join(env.subst("$PROJECT_DIR"), "tools"))
from build_data import build_data, gzip_data, del_data

def copy_gzip_data(source, target, env):
  del_gzip_data(source, target, env)
  data_zip_dir = env.get("PROJECT_DATA_DIR")
  data_dir = data_zip_dir[:-3]
  web_interface_base_dir = os.path.join(data_dir[:-4], "webInterface")

  build_data(data_dir, web_interface_base_dir)
  gzip_data(data_dir, data_zip_dir)

def del_gzip_data(source, target, env):
  print("Clearing zipped files")
  del_data(env.get("PROJECT_DATA_DIR"))

env.AddPreAction("$BUILD_DIR/littlefs.bin", copy_gzip_data)
env.AddPostAction("$BUILD_DIR/littlefs.bin", del_gzip_data)
