handle SIG63 pass nostop

define print_vcpu_state
set $id = 0
while machine.vm_vcpus[$id] != 0
   if machine.vm_vcpus[$id].state != PARKED_IDLE
      set $state = $_as_string(machine.vm_vcpus[$id].state)
      printf "vcpu[%3d]->state \t%s\n", $id, $state
   end
   set $id++
end
