# 默认执行的目标是app.
.DEFAULT_GOAL = app

# Add necessary options if the target is a shared library
# 如果在外部定义了SHARE=1, 则表示要构建共享库.so文件.
ifeq ($(SHARE),1)
# 共享库的文件后缀是.so
SO = -so
CFLAGS  += -fPIC -fvisibility=hidden
LDFLAGS += -shared -fPIC
endif

# 获取pwd当前路径
WORK_DIR  = $(shell pwd)
# 构建文件存放路径: 在当前路径下新建build文件夹, 构建的目标存在这里.
BUILD_DIR = $(WORK_DIR)/build

# 头文件路径: 默认包含当前路径下的include文件夹.
INC_PATH := $(WORK_DIR)/include $(INC_PATH)
# 源文件存放路径
OBJ_DIR  = $(BUILD_DIR)/obj-$(NAME)$(SO)
# 最终生成的可执行文件路径. 其中NAME(共享库的文件名)在外部(其他.mk)定义. 这也说明build.mk是被include进其他mk文件的!!
BINARY   = $(BUILD_DIR)/$(NAME)$(SO)

# Compilation flags
# 如果CC=clang, 即使用clang编译器, 则CXX也使用clang++. 否则CXX使用g++.
ifeq ($(CC),clang)
CXX := clang++
else
CXX := g++
endif
# 链接器LD和CXX相同.
LD := $(CXX)
# 添加头文件路径到编译选项中.
INCLUDES = $(addprefix -I, $(INC_PATH))
CFLAGS  := -O2 -MMD -Wall -Werror $(INCLUDES) $(CFLAGS)
LDFLAGS := -O2 $(LDFLAGS)

OBJS = $(SRCS:%.c=$(OBJ_DIR)/%.o) $(CXXSRC:%.cc=$(OBJ_DIR)/%.o)

# Compilation patterns
# 这两个规则是模式规则. 用% 通配符表示任意文件名. 模式规则允许一个obj匹配多个src, 按照模式规则的先后顺序查找src是否满足.
# 针对.c文件. OBJ_DIR下的所有.o文件由对应的.c文件编译而来.
$(OBJ_DIR)/%.o: %.c
# 打印`+ CC [正在编译的文件名]`
	@echo + CC $<
# 运行bash命令`mkdir -p $OBJ_DIR`. 即创建OBJ_DIR文件夹. dir内置函数把$@变量(目标文件路径)去掉最后的文件, 转换成目录路径.
	@mkdir -p $(dir $@)
# gcc编译命令.
	@$(CC) $(CFLAGS) -c -o $@ $<
# 生成依赖文件. $(@:.o=.d)表示把目标文件后缀.o替换成.d, 即生成对应的依赖文件.
	$(call call_fixdep, $(@:.o=.d), $@)

# 针对.cc(c++)文件. linux内核中把c++文件后缀名写作.cc而不是.cpp...???
$(OBJ_DIR)/%.o: %.cc
	@echo + CXX $<
	@mkdir -p $(dir $@)
	@$(CXX) $(CFLAGS) $(CXXFLAGS) -c -o $@ $<
	$(call call_fixdep, $(@:.o=.d), $@)

# Depencies: 
-include $(OBJS:.o=.d)

# Some convenient rules

.PHONY: app clean

# 这里的app是伪目标(没有目标文件), 而且都没有recipe. make执行app的时候实际上唯一做的事就是确保依赖$(BINARY)存在, 然后就结束了. 所以其实完全是等同于指向$(BINARY)这个规则了. 这样包装一层设计有啥意义?
app: $(BINARY)


# 构建可执行文件BINARY. 依赖为OBJS和ARCHIVES(归档文件, 静态库.a文件).
$(BINARY):: $(OBJS) $(ARCHIVES)
# 打印`+ LD [正在链接的文件名]`
	@echo + LD $@
# 链接命令.
	@$(LD) -o $@ $(OBJS) $(LDFLAGS) $(ARCHIVES) $(LIBS)


clean:
	-rm -rf $(BUILD_DIR)
