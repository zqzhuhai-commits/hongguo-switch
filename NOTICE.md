# 红果短剧SAF版

本桌面版基于 https://github.com/N3urda/hongguoTV 的提交
`eea73e6a45569a2cbbb9440f7cd5290c459b0b70` 制作，目标功能参考 Kotlin 0.8.0。
这是独立移植客户端，不运行或模拟 APK，也不代表红果官方。

新增桌面界面、Electron 主进程、片单持久化、媒体连接层及接口适配按
GPL-3.0-only 提供；完整许可证见 LICENSE。

vendor/hongguo.cjs 保留原源码未修改，来源为 drpys 固定提交
22261adfa31435e3b3ef5a730b8a2a75bcaf1715；其 GPL-3.0 许可证和来源记录见
vendor/NOTICE.md、vendor/LICENSE.drpys。
漫剧榜单解析和视频编码适配参考原项目 Kotlin core。

Electron 与其附带的 Chromium / Node.js，以及 axios、cheerio、sm-crypto
保留各自许可证。运行包中的 LICENSE.electron.txt、LICENSES.chromium.html
及 resources/app.asar 中的依赖 LICENSE 文件属于本发行包。

请将此文件及完整对应源码与软件一起分发。对应源码包包含 desktop/ 全部源文件、
固定依赖锁文件和构建说明。未配置 Windows Authenticode 发布签名。

## 新版取流协议参考

PhoneSource 的请求与响应兼容实现参考 qq294716498/HongguoTV-Phone-TV-NoAds 的公开实现（commit c356be4d1dccc187ece42aecf88a4688ef91d67e），重点核对 native/core/provider_hongguo_app.go、provider_hongguo_sign.go、provider_hongguo_native_media.go 和 provider_hongguo_playback.go。Switch 适配代码以 JavaScript 实现，复用本项目的原生 HTTP、AES 与播放器，不包含其 Flutter 页面、Go 运行时或 Android 安装包。

参考：https://github.com/qq294716498/HongguoTV-Phone-TV-NoAds

1.1.1: HEIC primary-item extraction in native/heic.hpp is adapted from HongguoTV-Phone-TV-NoAds native/core/image_heif.go (GPL-3.0), release app-v0.1.17-42. Image pixels are decoded with the existing FFmpeg libraries. Playback request signing/content types follow the same release.
