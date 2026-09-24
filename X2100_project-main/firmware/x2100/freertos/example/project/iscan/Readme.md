1. 将x2600e_nor_iscan_project_defconfig 复制到 freertos/configs/ 目录下

2. 将当前目录的的所有文件复制到 vendor/ 目录下，并且覆盖makefile

3. 修改 freertos/third_party/lvgl/lv_conf_template.h  下面的宏为“oxff00ff”

   ```c
   #define LV_COLOR_CHROMA_KEY lv_color_hex(0xff00ff)        
   ```

   

4. 在freertos/ 目录下：

   ```makefile
   make x2600e_nor_iscan_project_defconfig
   make
   ```

   

5. 制作存放UI资源的的文件系统(以1M为例)

   ```shell
   mkfs.fat -S 4096 -C fat-test.img 1024
   
   参数解释:
   -S logical-sector-size 逻辑扇区的大小
   -C 创建目标文件,用此选项必须给出<block-count>单位是kBytes
   命令效果:在当前目录产生一个.img文件,这是一个空镜像
   
   #创建文件夹
   mkdir fs_tmp
   
   #挂载文件系统
   sudo mount -t vfat fat-test.img ./fs_tmp
   
   #将ui资源文件复制到该目录下如:
   sudo cp resource/* -rf ./fs_tmp
   
   #取消挂载
   sudo umount ./fs_tmp
   ```

   

6. 
   烧录，文件系统分区创建为userdata，将rtos-with-zero.bin 和 fat-test.img 烧录进板子