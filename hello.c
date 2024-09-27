#include "types.h"
#include "stat.h"
#include "user.h"

int
main(void)
{
  // Use the write system call to print "Hello world\n" to stdout (file descriptor 1)
  printf(1, "Hello world\n");

  // Exit the program gracefully
  exit();
}
