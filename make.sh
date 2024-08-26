#!/bin/bash

DEBUG_LEVEL=${1:-release}
if [ "${DEBUG_LEVEL}" != "release" -a "${DEBUG_LEVEL}" != "fastdebug" -a "${DEBUG_LEVEL}" != "debug" ]; then
    echo "Invalid debug level, please select from release/fastdebug/debug"
    exit 1
fi
[ "${DEBUG_LEVEL}" = "debug" ] && DEBUG_LEVEL=slowdebug

SRC_PATH=$(dirname $(realpath $0))
DOCKER_EXIST=$(which docker &>/dev/null && echo 1 || echo 0)
DOCKER_IMAGE="adoptopenjdk/centos7_build_image:latest"
if [ "${DOCKER_EXIST}" -eq 0 ];then
    GCC_VERSION=$(gcc --version | head -n 1 | awk -F 'GCC)' '{print $NF}' | cut -d '.' -f 1 | sed 's/ //g')
    if [ "${GCC_VERSION}" -ne 11 ];then
        echo "Gcc major version ${GCC_VERSION}, expect 11.2"
        exit 1
    fi
    JAVA_VERSION=$(java -version 2>&1 | head -n 1 | cut -d '.' -f 1 | cut -d '"' -f 2)
    if [ "${JAVA_VERSION}" -ne 20 -a "${JAVA_VERSION}" -ne 21 ];then
        echo "Boot jdk major version should be 20 or 21"
        exit 1
    fi
fi

JTREG_PATH=/tmp/jtreg-6.1
JTREG_PACK=${JTREG_PATH}/jtreg_6_1.tar.gz
[ ! -d ${JTREG_PATH} ] && mkdir -p ${JTREG_PATH}
[ ! -d ${JTREG_PATH}/bin ] && curl -L -C - --retry 5 -o ${JTREG_PACK} https://compiler-tools.oss-cn-hangzhou.aliyuncs.com/jtreg/jtreg_6_1.tar.gz
[ -f ${JTREG_PACK} ] && tar xf ${JTREG_PACK} --strip-components=1 -C ${JTREG_PATH}
[ -f ${JTREG_PACK} ] && rm -rf ${JTREG_PACK}

NEW_JAVA_HOME="${SRC_PATH}/build/linux-$(arch)-server-${DEBUG_LEVEL}/images/jdk"
BUILD_COMMAND="export LDFLAGS_EXTRA='${LDFLAGS_EXTRA}' && \
export BASIC_LDFLAGS_JDK_LIB_ONLY='${BASIC_LDFLAGS_JDK_LIB_ONLY}' && \
export AJDK_LDFLAGS='${AJDK_LDFLAGS}' && \
export LD_LIBRARY_PATH='${LD_LIBRARY_PATH}' && \
export CC=/usr/local/gcc11/bin/gcc-11.2 && \
export CXX=/usr/local/gcc11/bin/g++-11.2 && \
bash ./configure --with-freetype=system \
--enable-unlimited-crypto \
--with-jvm-variants=server \
--with-debug-level=${DEBUG_LEVEL} \
--with-vendor-name='Alibaba' \
--with-vendor-url='http://www.alibabagroup.com' \
--with-vendor-bug-url='mailto:jvm@list.alibaba-inc.com' \
--with-version-opt='Alibaba' \
--with-version-string='21.0.1.0.1' \
--with-version-build='0' \
--with-version-date='$(date +%Y-%m-%d)' \
--with-zlib=system \
--with-jvm-features=shenandoahgc \
--with-boot-jdk=/usr/lib/jvm/jdk-20 \
--with-macosx-bundle-build-version=0 && \
make CONF=${DEBUG_LEVEL} LOG=cmdlines JOBS=$(expr $(nproc) / 2) images && \
${NEW_JAVA_HOME}/bin/java -version"

if [ "${DOCKER_EXIST}" -eq 1 ];then
    U_ID=$(id -u ${USER})
    docker run -it -u ${U_ID}:${U_ID} --network host -v ${JTREG_PATH}:${JTREG_PATH} -v ${JEMALLOC_LIB}:${JEMALLOC_LIB} -v /etc/hosts:/etc/hosts:ro -v /etc/group:/etc/group:ro -v /etc/passwd:/etc/passwd:ro -v /etc/shadow:/etc/shadow:ro -v ${SRC_PATH}:${SRC_PATH} -w ${SRC_PATH} ${DOCKER_IMAGE} /bin/bash -c "${BUILD_COMMAND}"
else
    ${BUILD_COMMAND}
fi
