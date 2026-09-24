
include_path := $(filter -I%, $(CFLAGS))
CFLAGS := $(filter-out -I%, $(CFLAGS))
include_path_abs := $(filter -I/%, $(include_path))
include_path := $(filter-out -I/%, $(include_path))
include_path := $(patsubst -I%,-I$(TOPDIR)%, $(include_path))

CFLAGS := $(CFLAGS) $(include_path_abs) $(include_path)

include_path := $(filter -I%, $(CXXFLAGS))
CXXFLAGS := $(filter-out -I%, $(CXXFLAGS))
include_path_abs := $(filter -I/%, $(include_path))
include_path := $(filter-out -I/%, $(include_path))
include_path := $(patsubst -I%,-I$(TOPDIR)%, $(include_path))

CXXFLAGS := $(CXXFLAGS) $(include_path_abs) $(include_path)

show_c_flags:
	$(Q)echo
	$(Q)echo
	$(Q)echo $(CFLAGS)

show_cxx_flags:
	$(Q)echo
	$(Q)echo
	$(Q)echo $(CXXFLAGS)

