CFLAGS += -Wl,--gc-sections -flax-vector-conversions
CXXFLAGS += -Wl,--gc-sections -flax-vector-conversions

#-------------------------------------------------------
package_name = libfacedet
package_depends =
package_builtin_src = third_party/libfacedet/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------

ifeq ($(CONFIG_LIBFACEDET_SDK),y)
CFLAGS += -Ithird_party/libfacedet/include/face_sdk/
package_lib-y += third_party/libfacedet/lib/face_sdk/libfacerec_sdk.a
package_lib-y += third_party/libfacedet/lib/face_sdk/libjzdl.a
package_lib-y += third_party/libfacedet/lib/face_sdk/libmxu_calib3d.a
package_lib-y += third_party/libfacedet/lib/face_sdk/libmxu_core.a
package_lib-y += third_party/libfacedet/lib/face_sdk/libmxu_features2d.a
package_lib-y += third_party/libfacedet/lib/face_sdk/libmxu_imgproc.a
package_lib-y += third_party/libfacedet/lib/face_sdk/libmxu_merge.a
package_lib-y += third_party/libfacedet/lib/face_sdk/libmxu_version.a
package_lib-y += third_party/libfacedet/lib/face_sdk/libmxu_video.a
endif

ifeq ($(CONFIG_LIBFACEDET_SDK_MODEL_SEPARATE),y)
CFLAGS += -Ithird_party/libfacedet/include/face_sdk_model_separate/
package_lib-y += third_party/libfacedet/lib/face_sdk_model_separate/libfacerec_sdk.a
package_lib-y += third_party/libfacedet/lib/face_sdk_model_separate/libjzdl.a
package_lib-y += third_party/libfacedet/lib/face_sdk_model_separate/libmxu_calib3d.a
package_lib-y += third_party/libfacedet/lib/face_sdk_model_separate/libmxu_core.a
package_lib-y += third_party/libfacedet/lib/face_sdk_model_separate/libmxu_features2d.a
package_lib-y += third_party/libfacedet/lib/face_sdk_model_separate/libmxu_imgproc.a
package_lib-y += third_party/libfacedet/lib/face_sdk_model_separate/libmxu_version.a
endif

