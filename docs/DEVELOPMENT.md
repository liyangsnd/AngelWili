# AngelWili 开发交接

AngelWili 基于 wiliwili，支持虎牙、斗鱼游客直播。当前不开发平台登录或账号同步。本地收藏、最近观看不依赖账号。

## 已实现

- 平台/分类选择、分类搜索、连续房间列表、刷新和手动房间号。
- 两平台原生游客解析，直播模式、清晰度切换和有限重连。
- 普通文字弹幕及有界队列、TLS 校验、连接停止与共享滚动渲染。
- 本地收藏、最近观看和卡片收藏星标。
- 与原版隔离的配置目录。

入口位于首页左侧“首页、动态、我的”下方的“直播”图标；点击或确认后打开双平台列表，设置页不再保留重复入口。

2026-11-16：用户确认 Mac 首页入口交互与默认灰色/选中粉色图标效果通过。未重新生成 NRO，真机调试仍暂停。

操作：列表 X 选平台、L 选分类、Y 输入房间、R 收藏/取消、减号打开本地记录、加号刷新。播放器 R 选清晰度、加号重试。底部显示操作提示。

## 构建

以下命令从本 Git 仓库根目录执行。

### macOS

```sh
brew install cmake mpv webp
cmake -B cmake-build-macos -DPLATFORM_DESKTOP=ON -DANGELWILI_BUILD_TESTS=ON
cmake --build cmake-build-macos --target wiliwili -j4
```

```sh
cd cmake-build-macos
./wiliwili --live-rooms
```

### Switch

需要可运行的 Docker，Apple Silicon 可使用 Colima。必须使用 wiliwili 指定的 FFmpeg/MPV 网络播放版。

```sh
docker run --rm --cpus=2 -v "$PWD:/data" devkitpro/devkita64:20251117 bash -c '
  set -e
  cd /data
  for pkg in switch-ffmpeg-7.1-1-any.pkg.tar.zst switch-libmpv-0.36.0-3-any.pkg.tar.zst; do
    if [ ! -f "$pkg" ]; then
      curl -fLO "https://github.com/xfangfang/wiliwili/releases/download/v0.1.0/$pkg"
    fi
  done
  dkp-pacman -U --noconfirm switch-ffmpeg-7.1-1-any.pkg.tar.zst switch-libmpv-0.36.0-3-any.pkg.tar.zst
  cmake -B cmake-build-switch -DPLATFORM_SWITCH=ON -DCMAKE_BUILD_TYPE=Release -DBUILTIN_NSP=OFF -DBRLS_UNITY_BUILD=ON -DCMAKE_UNITY_BUILD_BATCH_SIZE=16 -DSWITCH_NRO_TITLE=AngelWili -DSWITCH_NRO_BASENAME=AngelWili -DANGELWILI_CONFIG_NAME=angelwili
  cmake --build cmake-build-switch --target wiliwili.nro -j2
'
```

产物：`cmake-build-switch/AngelWili.nro`。不包含 NSP 转发器，可与原版 NRO 并存。

首次配置需要下载固定哈希的 Duktape、Mac mbedTLS 和 CA 文本；Switch TLS 复用容器中的系统 mbedTLS。对应许可随 NRO 资源打包。

## 测试

```sh
cmake --build cmake-build-macos --target huya_test douyu_test room_list_test room_library_test huya_danmaku_test douyu_danmaku_test -j4
./cmake-build-macos/huya_test
./cmake-build-macos/douyu_test
./cmake-build-macos/room_list_test
./cmake-build-macos/room_library_test
./cmake-build-macos/huya_danmaku_test
./cmake-build-macos/douyu_danmaku_test
```

真实接口测试依赖房间正在直播且网络可达，房间可更换：

```sh
./cmake-build-macos/huya_test 30440855 --stream
./cmake-build-macos/douyu_test 9999
./cmake-build-macos/room_list_test --live
./cmake-build-macos/huya_danmaku_test 998
./cmake-build-macos/douyu_danmaku_test 9999
```

历史协作过程与用户验收反馈见 `CHAT_HISTORY.md`。最新安排：暂停真机调试，优先查看 Mac 效果，不运行长时间压力测试。Mac 收藏角标已通过用户反馈；当前旧 NRO 未包含最后的角标改动，需要重新构建。

## 数据与限制

Switch 配置：`/config/angelwili`。Mac 配置：`~/Library/Application Support/angelwili`。本地记录为 `live_rooms.json`，不会与平台关注列表同步。

不要提交配置、Cookie、令牌、私钥、临时直播地址、构建目录或测试视频。上游播放器日志可能包含带签名的流地址，分享前需脱敏。

平台公开接口可能变化；清晰度名称或码率不是实际分辨率证明。两小时稳定性、所有直播格式和码率的兼容性尚未完成压力验收。

Git：`origin` 为 https://github.com/liyangsnd/AngelWili.git，`upstream` 为原 wiliwili 仓库，当前分支 `yoga`。