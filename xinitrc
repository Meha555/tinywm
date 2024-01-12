# xinitrc file for testing our window manager.

# 1. Start some programs to play with.
# xterm && sleep 2 &
#./color_demo.exe &
./cursor_demo.exe &
#./event_demo.exe &
./font_demo.exe &
#./pixmap_demo.exe &
# xclock &
xeyes &

# 2. Start our window manager.
export GLOG_logtostderr=1
valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --log-file=log.txt ./build/tinywm
#exec ./build/tinywm
