# tinywm

#### 介绍

仿照basic_wm，使用xcb写的X11简易窗口管理器。

目前代码是同步的，如果要改成异步的，应该是将所有 `errorHandler()` 部分改成不带 `_checked()` 后缀的API，然后在事件循环中添加 `errorHandler()` 的逻辑。

#### 安装依赖

```shell
sudo apt-get install libxcb-dev libxcb-keysym1-dev
```

#### 运行

```shell
./run.sh
```

#### TODO

- [ ] 添加标题栏、最小化最大化关闭按钮
- [ ] X 协议命令原语