BEGIN {
   syscall_names_string = "mmap,munmap,mprotect,munprotect,madvise,brk";
   split(syscall_names_string, syscall_names, ",");

   for(name in syscall_names) {
			 syscall_counts[syscall_names[name]] = 0;
   }
}

$7 == "syscall" {
   strace_output_found = 1;
}

strace_output_found {
   if($NF in syscall_counts) {
	    syscall_counts[$NF] += $4;
   }
}

END {
   if(!strace_output_found) {
      print "strace output not found!";
      exit;
   }

   for(name in syscall_names) {
      printf("%10s = %5d\n", syscall_names[name], syscall_counts[syscall_names[name]]);
   }
}
