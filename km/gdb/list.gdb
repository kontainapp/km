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
     print_list &mmaps.free
end

define print_tailq
if $argc == 0
      help print_tailq
      return
   else
      set $head = $arg0
   end

   set $item = $head->tqh_first
   set $last_end = 0
   set $count = 0
   while $item != 0
      if $count == 0
         set $distance = 0
      else
         set $distance =  $item->start - $last_end
      end
      printf "%3d start= 0x%lx size = %ld MiB (%ld) prot 0x%x flags 0x%x distance %ld MiB (0x%lx)\n", \
                $count, $item->start, $item->size / (1024*1024), $item->size, \
                $item->protection, $item->flags, $distance / (1024*1024) ,  $distance
      set $last_end = $item->start + $item->size
      set $item=$item->link->tqe_next
      set $count++
   end
   printf "total %d" , $count
end

document print_tailq
   Prints TAILQ info. Assumes TAILQ entry name is 'link'
     Syntax: print_tailq &LIST
   Examples:
     print_tailq &mmaps.free
end

define km_mmaps
   print "mmaps FREE list:"
   print_tailq &mmaps.free
   print "mmaps BUSY list:"
   print_tailq &mmaps.busy "Busy list"
   printf ".tbrk= 0x%lx\n", machine.tbrk
end
