#!/usr/bin/sh

# 代理URL，必须最后含有字符/，没有时请设置为空，请注意有两处，后面还有一处
# 这里用的免费的代理可能失效，如果失效，请注释掉下一行
PROXY_URL="https://scoop.201704.xyz/"

OS_VER="$(lsb_release -sr)"

# 切换时区东八区
sudo timedatectl set-timezone Asia/Shanghai

# 更新中科大的源
if [ "$OS_VER" = "24.04" ]; then
	sudo sed -E -i -e 's/(archive|security).ubuntu.com/mirrors.ustc.edu.cn/g' /etc/apt/sources.list.d/ubuntu.sources
else
	sudo sed -E -i -e 's/(archive|ports).ubuntu.com/mirrors.ustc.edu.cn/g' -e '/security.ubuntu.com/d' /etc/apt/sources.list
fi

# 更新apt
sudo apt-get update

# 安装一系列的软件
sudo apt-get install -y zsh vim git wget curl python3 sudo
sudo apt-get install -y software-properties-common apt-utils build-essential gcc g++ clang clangd clang-format clang-tidy bear llvm libomp-dev libtool
sudo apt-get install -y cmake ninja-build
sudo apt-get install -y graphviz graphviz-dev
sudo apt-get install -y flex bison dos2unix
sudo apt-get install -y gdb lldb gdbserver gdb-multiarch
sudo apt-get install -y openjdk-17-jdk dotnet-sdk-6.0
# ARM64的交叉编译环境
sudo apt-get install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
# ARM32的交叉编译环境
sudo apt-get install -y gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
# RISCV64的交叉编译环境
sudo apt-get install -y gcc-riscv64-linux-gnu g++-riscv64-linux-gnu
# QEMU用户态模拟器
sudo apt-get install -y qemu-user-static
# SSH服务器
sudo apt-get install -y openssh-server
# 文档生成工具
sudo apt-get install -y doxygen
# LaTeX相关包，用于doxygen生成pdf文档，默认不安装
# sudo apt-get install -y texlive-lang-chinese texlive-lang-english texlive-latex-extra texlive-science texlive-plain-generic

# 编译安装antlr 4.12.0
if [ -f /usr/local/bin/antlr-4.12.0-complete.jar ]; then
	echo "antlr-4.12.0-complete.jar exists, skipping"
else
	sudo wget -O /usr/local/bin/antlr-4.12.0-complete.jar ${PROXY_URL}https://github.com/antlr/website-antlr4/raw/refs/heads/gh-pages/download/antlr-4.12.0-complete.jar
	sudo chmod +x /usr/local/bin/antlr-4.12.0-complete.jar
fi

if [ -f /usr/local/lib/libantlr4-runtime.so.4.12.0 ]; then
	echo "antlr4-cpp-runtime-4.12.0 exists, skipping"
else
	wget -O ~/antlr4-cpp-runtime-4.12.0-source.zip ${PROXY_URL}https://github.com/antlr/website-antlr4/raw/refs/heads/gh-pages/download/antlr4-cpp-runtime-4.12.0-source.zip
	unzip ~/antlr4-cpp-runtime-4.12.0-source.zip -d ~/antlr4-cpp-runtime-4.12.0-source
	cd ~/antlr4-cpp-runtime-4.12.0-source || exit 1
	cmake -B build -S . -G Ninja -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Debug -DANTLR_BUILD_CPP_TESTS=OFF
	cmake --build build --parallel
	sudo cmake --install build
	rm -rf ~/antlr4-cpp-runtime-4.12.0-source
	rm -f ~/antlr4-cpp-runtime-4.12.0-source.zip
fi

# 安装oh-my-zsh
if [ -d ~/.oh-my-zsh ]; then
	echo "oh-my-zsh exists, skipping"
else
	git clone ${PROXY_URL}https://github.com/robbyrussell/oh-my-zsh.git ~/.oh-my-zsh
	cp ~/.oh-my-zsh/templates/zshrc.zsh-template ~/.zshrc
	git clone ${PROXY_URL}https://github.com/zsh-users/zsh-autosuggestions ~/.oh-my-zsh/custom/plugins/zsh-autosuggestions
	git clone ${PROXY_URL}https://github.com/zsh-users/zsh-syntax-highlighting.git ~/.oh-my-zsh/custom/plugins/zsh-syntax-highlighting
	sed -i 's/^plugins=(/plugins=(zsh-autosuggestions zsh-syntax-highlighting z /' ~/.zshrc
	usermod -s /usr/bin/zsh "$USER"
	exec /usr/bin/zsh
fi
