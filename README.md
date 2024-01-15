# tinywm

#### 介绍

仿照basic_wm，使用xcb写的X11简易窗口管理器。

目前完成度80%左右，键盘那块我没弄了，没弄明白xcb提供哪些关于键盘和鼠标的API和掩码。

目前代码是同步的，如果要改成异步的，应该是将所有 `errorHandler()` 部分改成不带 `_checked()` 后缀的API，然后在事件循环中添加 `errorHandler()` 的逻辑。

#### 安装依赖

```shell
sudo apt-get install libxcb1-dev xcb-keysyms libxcb-util-dev libxcb-icccm4-dev
```

#### 运行

```shell
./run.sh
```

#### 可供参考的材料

以下是我在网上找到的wm项目，不过我没看，因为我是写完了才找到的😥..

- [tinywm (incise.org)](http://incise.org/tinywm.html)
- [Meha555/basic_wm: 简易X11窗口管理器实现 (github.com)](https://github.com/Meha555/basic_wm)

#### TODO

- [x] 添加标题栏
- [ ] 最小化最大化关闭按钮
- [x] X 协议命令原语