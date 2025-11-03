实验环境：ubuntu22.04

# 1.安装依赖：
--sudo apt-get install cmake ninja-build libelf-dev libomp-12-dev build-essential  

# 2.下载downmem模拟器源码：
-- git clone https://github.com/jingge815/downmem.git   
-- cd downmem  
-- git checkout feature/develop-compiler  

# 3.下载编译器源码：
-- cd ..  
-- git clone https://github.com/llvm/llvm-project.git  // 因网络限制国内偶尔下载不了，可以采用国内的镜像安装：git clone https://mirrors.tuna.tsinghua.edu.cn/git/llvm-project.git  

# 4.运行编译脚本:
-- ./cmake/rvupmem-toolchainbuild.sh "安装路径" "已下载llvm源码路径" //路径必须是绝对路径，运行时间可能需要几个小时  
-- 具体的执行步骤是：1.编译llvm15;2.利用llvm15编译llvm20; 3.编译rv运行时；4.利用llvm20编译downmem；5.运行测试用例脚本。  
-- 例如：./cmake/rvupmem-toolchainbuild.sh /home/fengjingge/src/downmem/new2-downmem/installed /home/fengjingge/src/downmem/new2-downmem/llvm-project   

# 5.编译执行rv指令集downmem
-- ./rvRunTests.sh "安装的LLVM路径"， 例如：./rvRunTests.sh /home/fengjingge/src/downmem/new2-downmem/installed  

# 6.编译执行upmem指令集downmem
-- ./ummRunTests.sh "upmem路径" ，例如： ./ummRunTests.sh /home/fengjingge/src/downmem/new2-downmem/downmem/third-party/upmem  

# 7.执行定制化脚本
-- 注意：必须先执行完成第5，6步，生成dmm库  
-- cd dmm-app-example  
-- 设置build.sh文件中的BASE_PATH为当前downmem模拟器的首地址，例如：BASE_PATH="/home/fengjingge/src/downmem/new2-downmem/downmem"    
-- 设置build.sh文件中CMAKE_C_COMPILER变量为llvm编译器路径，例如：-DCMAKE_C_COMPILER=${BASE_PATH}/../installed/bin/clang  
-- 编译程序，执行下面命令： ./build.sh 程序命令 线程数， 例如：./build.sh mydev2 16  
-- 设置环境变量， source setup_env.sh  
-- 执行程序，./build/myhost2 34 32  devApp/objdumps/mydev2.objdump  




