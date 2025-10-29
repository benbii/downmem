实验环境：ubuntu22.04

1.安装依赖：sudo apt-get install cmake ninja-build libelf-dev libomp-dev libomp-12-dev build-essential

2.下载downmem模拟器源码：git clone https://github.com/jingge815/downmem.git 

3.下载编译器源码：git clone https://github.com/llvm/llvm-project.git  // 因网络限制国内偶尔下载不了，可以采用国内的镜像安装：git clone https://mirrors.tuna.tsinghua.edu.cn/git/llvm-project.git

4.运行编译脚本，./cmake/rvupmem-toolchainbuild.sh "安装路径" "已下载llvm源码路径" //路径必须是绝对路径，运行时间可能需要几个小时
具体的执行步骤是：1.编译llvm15;2.利用llvm15编译llvm20; 3.编译rv运行时；4.利用llvm20编译downmem；5.运行测试用例脚本。
例如：./cmake/rvupmem-toolchainbuild.sh /home/fengjingge/src/downmem/new2-downmem/installed /home/fengjingge/src/downmem/new2-downmem/llvm-project 

5.编译执行rv指令集downmem
./rvRunTests.sh "安装的LLVM路径"， 例如：./rvRunTests.sh /home/fengjingge/src/downmem/new2-downmem/installed

6.编译执行upmem指令集downmem
./ummRunTests.sh "upmem路径" ，例如： ./ummRunTests.sh /home/fengjingge/src/downmem/new2-downmem/downmem/third-party/upmem
