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
