#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
handle SIG63 pass nostop

#
# print BSD list content (see 'man queue')
# Can be used for printing mmaps free/busy query
#

define print_list
if $argc == 0
      help print_list
      return
   else
      set $head = $arg0
   end

   set $item = $head->lh_first
   while $item != 0
      printf "start= 0x%lx size = %d MiB\n", $item->start, $item->size/1024/1024
      set $item=$item->link->le_next
   end
end

document print_list
   Prints LIST info. Assumes LIST entry name is 'link'
     Syntax: print_list &LIST
   Examples:
     print_list &machine.mmaps.free
end

define print_tailq
if $argc == 0
      help print_tailq
      return
   else
      set $head = $arg0
   end

   set $item = $head->tqh_first
   set $last_end = $item->start
   set $count = 1
   while $item != 0
      set $distance = $item->start - $last_end
      printf "%3d 0x%-12lx next 0x%-12lx start= 0x%lx size = 0x%05x (%08ld) prot 0x%x flags 0x%x fn %s km_fl 0x%x distance %u (%lu)\n", \
               $count, $item, $item->link->tqe_next, $item->start/0x1000ul, $item->size/0x1000ul, $item->size, \
               $item->protection, $item->flags, $item->filename, $item->km_flags.data32, $distance/0x1000ul, $distance
      set $count++
      set $last_end = $item->start + $item->size
      set $item=$item->link->tqe_next
   end
end

document print_tailq
   Prints TAILQ info. Assumes TAILQ entry name is 'link'
     Syntax: print_tailq &LIST
   Examples:
     print_tailq &machine.mmaps.free
end

define print_mmaps
   printf "................................       tbrk= 0x%lx\n", machine.tbrk
   if machine.mmaps.free.tqh_first == 0
      print "mmap FREE list empty"
   else
      print "mmaps FREE list:"
      print_tailq &machine.mmaps.free
   end

   if machine.mmaps.busy.tqh_first == 0
      print "mmap BUSY list empty"
   else
      print "mmaps BUSY list:"
      print_tailq &machine.mmaps.busy "Busy list"
   end
end

define show_vcpus
   set $cpu = 0
   printf "VCPU   FD STATE             RIP              RSP\n"
   while $cpu < 288
      if machine.vm_vcpus[$cpu] == 0
         loop_break
      end
      printf "%4x %4x %4x %16llx %16llx\n", \
         machine.vm_vcpus[$cpu].vcpu_id, \
         machine.vm_vcpus[$cpu].kvm_vcpu_fd, \
         machine.vm_vcpus[$cpu].state, \
         machine.vm_vcpus[$cpu].regs.rip, \
         machine.vm_vcpus[$cpu].regs.rsp
      set $cpu = $cpu + 1
   end
end

document show_vcpus
   Prints VCPU rip and rsp
   Syntax:
      show_vcpus
   Examples:
     show_vcpus
end

define show_guest_args
   set $cpu = 0
   printf "VCPU        ARGSP           HC_RET             ARG1             ARG2\n"
   printf "                    ARG3             ARG4             ARG5             ARG6\n"
   while $cpu < 288
      if machine.vm_vcpus[$cpu] == 0
         loop_break
      end
      set $p = (uint64_t *)km_hcargs
      set $i = $cpu * 8
      set $km_hc_args = (struct km_hc_args *)$p[$i]

      printf "%4x %p %16llx %16llx %16llx\n", \
         $cpu, $p[$i], $km_hc_args->hc_ret, \
         $km_hc_args->arg1, $km_hc_args->arg2
      printf "        %16llx %16llx %16llx %16llx\n", \
         $km_hc_args->arg3, $km_hc_args->arg4, \
         $km_hc_args->arg5, $km_hc_args->arg6
      set $cpu = $cpu + 1
   end
end

document show_guest_args
   Prints guest per vcpu hcall arguments
   Syntax:
      show_guest_args
   Examples:
     show_guest_args
end
