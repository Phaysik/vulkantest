#!/bin/bash

setUpGCC() {
    echo "Setting up gcc, g++, and gcov"

    curl -L -o gcc-latest.tar.gz https://ftp.gnu.org/gnu/gcc/gcc-$1/gcc-$1.tar.gz
    mkdir -p gcc-latest
    tar -xvzf gcc-latest.tar.gz -C gcc-latest --strip-components=1

    sudo rm -rf gcc-latest.tar.gz
    cd gcc-latest
    ./contrib/download_prerequisites
    cd ..

    directory="/usr/local/gcc-$2"
    sudo mkdir -p $directory

    # clean any previous incomplete build
    if [ -d gcc-build ]; then
        rm -rf gcc-build
    fi

    mkdir -p gcc-build
    cd gcc-build

    ../gcc-latest/configure --prefix=$directory --enable-languages=c,c++ --disable-multilib --enable-bootstrap
    make -j$(nproc)
    sudo make install

    sudo ldconfig

    sudo update-alternatives --install /usr/bin/g++ g++ $directory/bin/g++ $2
    sudo update-alternatives --install /usr/bin/gcc gcc $directory/bin/gcc $2
    sudo update-alternatives --install /usr/bin/gcov gcov $directory/bin/gcov $2

    cd ..

    sudo rm -rf gcc-latest
    sudo rm -rf gcc-build

    sudo mv /lib/x86_64-linux-gnu/libstdc++.so.6 /lib/x86_64-linux-gnu/libstdc++.so-copy.6
    sudo mv /lib/x86_64-linux-gnu/libstdc++.so.6.0.32 /lib/x86_64-linux-gnu/libstdc++.so-copy.6.0.32

    sudo cp /usr/local/gcc-$2/lib64/libstdc++.so.6.0.34 /lib/x86_64-linux-gnu
    sudo ln -s libstdc++.so.6.0.32 /lib/x86_64-linux-gnu/libstdc++.so.6
    sudo ldconfig
}

setUpClangTools() {
    echo "Setting up clang-tidy, clang-format, and clangd"

    wget https://apt.llvm.org/llvm.sh
    sudo chmod +x llvm.sh
    sudo ./llvm.sh $1
    rm -rf ./llvm.sh
    sudo apt-get install -y clang-format clang-tidy clangd

    sudo update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-$1
    sudo update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-$1
    sudo update-alternatives --install /usr/bin/clangd clangd /usr/bin/clangd-$1
}

installDoxygen() {
    echo "Setting up doxygen"

    sudo wget https://www.doxygen.nl/files/doxygen-$1.linux.bin.tar.gz

    mkdir -p doxygen

    sudo tar -xvzf doxygen-$1.linux.bin.tar.gz -C doxygen --strip-components=1

    sudo rm -rf doxygen-$1.linux.bin.tar.gz

    cd doxygen
    cd bin

    sudo mv -f doxy* /usr/bin

    cd ..
    cd ..
    sudo rm -rf doxygen
}

checkSphinx() {
    packages=("sphinx" "breathe" "sphinx-book-theme" "sphinx-copybutton" "sphinx-autobuild" "sphinx-last-updated-by-git" "sphinx-notfound-page" "sphinxcontrib-spelling" "furo")

    all_packages_installed=true

    for package in "${packages[@]}"; do
        if ! pip show "$package" >/dev/null 2>&1; then
            echo "$package is not installed"
            all_packages_installed=false
        fi
    done

    if $all_packages_installed; then
        return 0  # All packages installed
    else
        return 1  # Some packages missing
    fi
}

setUpTracy() {
    echo "Setting up Tracy Profiler"

    # Download the latest version of Tracy
    url=$(curl -s https://api.github.com/repos/wolfpld/tracy/releases/latest | grep -o '"tarball_url": *"[^"]*"' | cut -d '"' -f 4)
    curl -L -o tracy-latest.tar.gz $url
    mkdir -p tracy-latest
    tar -xvzf tracy-latest.tar.gz -C tracy-latest --strip-components=1
    sudo rm -rf tracy-latest.tar.gz

    cd tracy-latest

    sudo apt-get install g++-11 gcc-11 libfreetype6-dev libcapstone-dev libegl1-mesa-dev libxkbcommon-dev libwayland-dev libdbus-1-dev libglfw3 libglfw3-dev wayland-protocols -y
    sudo cp -r /usr/include/freetype2/* /usr/include/
    sudo cp -r /usr/include/capstone/* /usr/include/
    sudo cp -r /usr/include/dbus-1.0/* /usr/include/
    sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-11 11
    sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 11

    # For installing the Client of Tracy
    mkdir build
    cd build
    cmake ..
    make
    sudo make install
    cd ..

    # For installing the Server of Tracy
    cd profiler
    mkdir build
    cd build
    cmake ..
    make CXX=g++-11 LEGACY=1
    mv Tracy-release Tracy-Server
    sudo cp Tracy-Server /usr/bin/

    # Remove Tracy files from local
    cd ../../../../
    sudo rm -rf tracy-latest

    sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-$2 $2
    sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-$2 $2
}

setUpConfigCat() {
    echo "Setting up Config Cat Feature Flags"

    git clone https://github.com/microsoft/vcpkg
    sudo ./vcpkg/bootstrap-vcpkg.sh
    sudo ./vcpkg/vcpkg install configcat

    cd vcpkg
    cd packages
    cd configcat_x64-linux

    sudo cp ./lib/* /usr/lib/
    sudo cp -r ./include/* /usr/include/

    cd ..
    cd curl_x64-linux

    sudo cp -r ./lib/* /usr/lib/
    sudo cp -r ./include/* /usr/include/

    cd ..
    cd hash-library_x64-linux

    sudo cp ./lib/* /usr/lib/
    sudo cp -r ./include/* /usr/include/

    cd ..
    cd ..
    cd ..
    sudo rm -rf vcpkg
}

installVulkan() {
    echo "Installing build essentials..."
    sudo apt-get update
    sudo apt-get install -y build-essential cmake ninja-build

    echo "Installing GLFW..."
    sudo apt-get install -y libglfw3-dev

    echo "Installing GLM..."
    sudo apt-get install -y libglm-dev

    echo "Installing tinyobjloader..."
    sudo apt-get install -y libtinyobjloader-dev || echo "tinyobjloader not found in apt, will need to be installed manually or via CMake FetchContent"

    echo "Installing stb..."
    sudo apt-get install -y libstb-dev || echo "stb not found in apt, will need to be installed manually or via CMake FetchContent"

    echo "Installing tinygltf..."
    sudo apt-get install -y libtinygltf-dev || echo "tinygltf not found in apt, will need to be installed manually or via CMake FetchContent"

    echo "Installing nlohmann-json..."
    sudo apt-get install -y nlohmann-json3-dev || echo "nlohmann-json not found in apt, will need to be installed manually or via CMake FetchContent"

    echo "Installing X Window System dependencies..."
    sudo apt-get install -y libxxf86vm-dev libxi-dev

    echo "Installing Vulkan packages..."
    sudo apt-get install -y libvulkan1

    echo "Removing any previous Vulkan SDK installation in ~/vulkansdk"
    sudo rm -rf ~/vulkansdk

    sudo mkdir -p ~/vulkansdk
    sudo wget https://sdk.lunarg.com/sdk/download/$1/linux/vulkansdk-linux-x86_64-$1.tar.xz

    sudo tar -xvf vulkansdk-linux-x86_64-$1.tar.xz -C ~/vulkansdk

    sudo rm -rf vulkansdk-linux-x86_64-$1.tar.xz

    cd ~/vulkansdk

    sudo cp -r ~/vulkansdk/$1/x86_64/lib/* /usr/lib/
    sudo cp -r ~/vulkansdk/$1/x86_64/include/* /usr/include/
    sudo cp -r ~/vulkansdk/$1/x86_64/bin/* /usr/bin/

    echo "Add the following to ~/.zshrc:"
    echo "source ~/vulkansdk/$1/setup-env.sh"
    echo ""
    echo "Run: source ~/.zshrc"
    echo ""
    echo "Verify installation by running: vkcube"
}

installBenchmark() {
    git clone https://github.com/google/benchmark.git
    cd benchmark

    sudo cmake -E make_directory "build"

    sudo cmake -DBENCHMARK_DOWNLOAD_DEPENDENCIES=on -DCMAKE_BUILD_TYPE=Release -S . -B "build"

    sudo cmake --build "build" --config Release --target install

    sudo cp /usr/local/lib/libbench* /usr/lib/
    sudo cp -r /usr/local/include/benchmark* /usr/include
    sudo cp /usr/local/lib/pkgconfig/benchmark* /usr/lib/pkgconfig/
    sudo cp -r /usr/local/lib/cmake/benchmark /usr/lib/cmake/
    sudo cp -r /usr/local/share/doc/benchmark /usr/share/doc/
    sudo cp -r /usr/local/share/googlebenchmark /usr/share

    cd ..
    sudo rm -rf benchmark
}

main() {
    echo "This shell file is set up to only work on Ubuntu operating systems"

    response="n"

    if [ -z "$1" ]; then
        read -p "Enter (Y/N) if you are running on Ubuntu and wish to auto install all packages required: " response
    fi

    # a or 'A' for automated running (For Github workflows ignoring long documentation, linting, and formatting installation)
    if [ "${response,,}" = "y" ] || [ "${1,,}" = "y" ] || [ "${response,,}" = "a" ] || [ "${1,,}" = "a" ]; then
        echo "Update and upgrading your packages (will require an elevated user's password)"
        sudo apt update && sudo apt upgrade -y

        echo "Installing all the required packages for all commands used in the Makefile"

        sudo apt-get install make cmake libgtest-dev libgmock-dev python3-pip docker-compose -y

        sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 10
        sudo update-alternatives --install /usr/bin/gcov gcov /usr/bin/gcov-14 14
        sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 10

        setUpConfigCat

        desired_version="15.2.0"
        gpp_priortiy="15"
        echo "Setting up g++"

        if [ "$(command g++ --version | grep -oP '\d+\.\d+\.\d+')" = "$desired_version" ]; then
            echo "g++-${desired_version} exists"
        elif [ "${1,,}" != "a" ] || ([ "${response,,}" != "a" ] && [ "${response,,}" != "n" ]); then
            setUpGCC $desired_version $gpp_priortiy
        else
            sudo apt-get install -y g++-13 # For automated running
            sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 13
        fi

        if [ -f "/usr/lib/libgtest.a" ] && [ -f "/usr/lib/libgtest_main.a" ]; then
            echo "Google Test already exists"
        else
            echo "Setting up Google Test"
            cd /usr/src/gtest
            sudo cmake CMakeLists.txt
            sudo make
            sudo cp ./lib/libgtest*.a /usr/lib
        fi

        if [ -f "/usr/lib/libbenchmark.a" ] && [ -f "/usr/lib/libbenchmark_main.a" ]; then
            echo "Google Benchmark already exists"
        else
            echo "Setting up Google Benchmark"
            installBenchmark
        fi

        # If not 'a' or 'A', set up documentation, formatting, and linting tools
        if [ "${response,,}" = "y" ] || [ "${1,,}" = "y" ]; then
            sudo apt-get install binutils valgrind graphviz flex bison libpcre3 libpcre3-dev lcov cppcheck xterm -y

            llvmVersionToDownload="22"

            if [ -x "$(command -v clang-tidy)" ] && [ -x "$(command -v clang-format)" ]; then
                echo "clang-tidy and clang-format already exists"

                clang_tidy_version=$(clang-tidy --version | awk '/LLVM version/ {print $4}')
                clang_tidy_desired_version="22.0.0"

                clang_format_version=$(clang-format --version | awk '{print $4}')
                clang_format_desired_version="22.0.0"


                if [[ "$clang_tidy_version" == "$clang_tidy_desired_version" ]] && [[ "$clang_format_version" == "$clang_format_desired_version" ]]; then
                    echo "clang-tidy version $clang_tidy_desired_version and clang-format version $clang_format_desired_version already exists"
                else
                    setUpClangTools $llvmVersionToDownload
                fi
            else
                setUpClangTools $llvmVersionToDownload
            fi

            doxygen_desired_version="1.16.1"
            if [ -x "$(command -v doxygen)" ]; then
                echo "doxygen already exists"

                doxygen_version=$(doxygen --version | awk '{print $1}')

                if [[ "$doxygen_version" == "$doxygen_desired_version" ]]; then
                    echo "doxygen version $doxygen_desired_version already exists"
                else
                    installDoxygen $doxygen_desired_version
                fi
            else
                installDoxygen $doxygen_desired_version
            fi

            checkSphinx
            status=$?

            if [ $status -eq 0 ]; then
                echo "All Sphinx packages are installed"
            else
                echo "Installing Sphinx and it's dependencies for documentation"
                pip3 install --upgrade pip --break-system-packages
                pip3 install sphinx breathe sphinx-book-theme sphinx-copybutton sphinx-autobuild sphinx-last-updated-by-git sphinx-notfound-page sphinxcontrib-spelling furo --break-system-packages
            fi

            if [ -x "$(command -v flawfinder)" ]; then
                echo "Flawfinder already exists"
            else
                pip3 install flawfinder --break-system-packages
            fi

            if [ -x "$(command -v Tracy-Server)" ]; then
                echo "Tracy-Server already exists"
            else
                setUpTracy $gpp_priortiy
            fi

            vulkan_desired_version="1.4.341.1"
            if [ -x "$(command -v vkcube)" ]; then
                echo "vkcube already exists"

                vulkan_version=$(vulkaninfo 2>/dev/null | grep -m1 'Vulkan Instance Version' | cut -d: -f2 | xargs)

                if [[ "$vulkan_version" == "$vulkan_desired_version" ]]; then
                    echo "vulkan version $vulkan_desired_version already exists"
                else
                    installVulkan $vulkan_desired_version
                fi
            else
                installVulkan $vulkan_desired_version
            fi
        fi
    else
        echo -e "\nBegin by installing make itself, and then look at the table below to find what other packages to install based on what commands you wish to run\n"

        col1_width=29
        col2_width=49
        col3_width=30

        # Print the table header
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "Makefile Command" "Makefile Command(s) it relies on" "Packages Required"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "----------------" "--------------------------------" "-----------------"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "compile" "" "make g++"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "release" "compile" "make"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "debug" "" "make g++"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "dev" "debug" "make"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "val" "" "make g++"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "valgrind" "val" "make valgrind"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "copy_and_run_tests" "-" "make"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "build_tests" ", copy_and_run_test" "make g++ libgmock-dev libgtest-dev (May require extra installation steps - Look at guide online)"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "lcov" "build_test" "make lcov"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "genhtml" "lcov" "make lcov"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "coverage" "genhtml" "make"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "tidy" "-" "make clang-tidy"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "check" "-" "make libpcre3 libpcre3-dev cppcheck"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "flawfinder" "-" "make pip"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "analysis" "tidy check flawfinder" "make"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "format" "-" "make clang-format"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "run_doxygen" "-" "make graphviz doxygen flex bison"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "profile" "dev" "make binutils"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "docs" "run_doxygen" "make sphinx breathe sphinx-book-theme sphinx-copybtton sphinx-autobuild sphinx-last-updated-by-git sphinx-notfound-page"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "tracy" "" "make g++-11 gcc-11 libfreetype6-dev libcapstone-dev libegl1-mesa-dev libxkbcommon-dev libwayland-dev libdbus-1-dev libglfw3 libglfw3-dev"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "gprof" "dev" "make binutils"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "profile" "gprof" "make"
        printf "%-${col1_width}s %-${col2_width}s %-${col3_width}s\n" "initialize_repo" "-" "make git"
    fi
}

main "$@"
