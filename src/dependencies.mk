
cpu= 
elf= mem
mem= 
controller= cpu
zircon= cpu elf mem trace controller color
zircon-wasm= cpu elf mem trace controller color

define make_depen
$(eval $1: $($1))
endef
map = $(foreach a,$(2),$(call $(1),$(a)))
define make_prereqs
$(call map,make_depen,cpu mem elf trace zircon zircon-wasm color controller)
endef
