#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>



#define SWITCH_TO_SHELL 20

int verbosity;



struct sort {
  int fd;
  off_t size;
  void *buffer;
};


static inline void 
shellsort(unsigned char *a, 
	  const int n, 
	  const int record_size, 
	  const int key_size) {
  int i, j;
  char temp[record_size];
  
  for (i=3; i < n; i++) {
    memcpy(&temp, &a[i * record_size], record_size);
    for(j=i; j>=3 && memcmp(a+(j-3)*record_size, &temp, key_size) >0; j -= 3) {
      memcpy(a+j*record_size, a+(j-3)*record_size, record_size);
    }
    memcpy(a+j*record_size, &temp, record_size);
  }
  
  for (i=1; i < n; i++) {
    memcpy(&temp, &a[i*record_size], record_size);
    for(j=i; j>=1 && memcmp(a+(j-1)*record_size, &temp, key_size) >0; j -= 1) {
      memcpy(a+j*record_size, a+(j-1)*record_size, record_size);
    }
    memcpy(a+j*record_size, &temp, record_size);
  }
}


void 
radixify(unsigned char *buffer, 
	 const int count, 
	 const int digit, 
	 const int char_start, 
	 const int char_stop, 
	 const int record_size, 
	 const int key_size, 
	 const int stack_size,
	 const int cut_off) {
  int counts[char_stop];
  int offsets[char_stop];
  int starts[char_stop];
  int ends[char_stop];
  int offset=0;
  char temp[record_size];
  int target, x, a, b;
  int stack[stack_size];
  int stack_pointer;
  int last_position, last_value, next_value;

  if (verbosity && digit == 0)
    fprintf(stderr, "radixify(count=%d, digit=%d, char_start=%d, char_stop=%d, record_size=%d, key_size=%d, stack_size=%d, cut_off=%d)\n", count, digit, char_start, char_stop, record_size, key_size, stack_size, cut_off);
  
  for (x=char_start; x<=char_stop; x++) {
    counts[x] = 0;
    offsets[x] = 0;
  }
  
  // Compute starting positions
  for (x=0; x<count; x++) {
    int c = buffer[x*record_size + digit];
    counts[c] += 1;
  }

  // Compute offsets
  offset = 0;
  for(x=char_start; x<=char_stop; x++) {
    offsets[x] = offset;
    starts[x] = offsets[x];
    offset += counts[x];
  }

  for(x=char_start; x<=char_stop; x++) {
    ends[x] = offsets[x+1];
  }
  ends[char_stop-1] = count;

  for(x=char_start; x<=char_stop; x++) {
    while (offsets[x] < ends[x]) {
      
      if (buffer[offsets[x] * record_size + digit] == x) {
	offsets[x] += 1;
      } else {
	int p=0;
	stack_pointer=0;
	stack[stack_pointer] = offsets[x];
	stack_pointer += 1;
	target = buffer[offsets[x] * record_size + digit];
	while( target != x && stack_pointer < stack_size )
	  {
	    stack[stack_pointer] = offsets[target];
	    offsets[target] += 1;
	    target = buffer[stack[stack_pointer] * record_size + digit];
	    stack_pointer++;
	  };
	if (stack_pointer != stack_size) {
	  offsets[x] += 1;
	}
	stack_pointer--;
	memcpy(&temp, &buffer[stack[stack_pointer] * record_size], record_size);
	while (stack_pointer) {
	  memcpy(&buffer[stack[stack_pointer] * record_size], &buffer[stack[stack_pointer-1] * record_size], record_size);
	  stack_pointer--;
	}
	memcpy(&buffer[stack[0] * record_size], &temp, record_size);
      }
    }
  }
    
  if (digit < cut_off) {
    for(x=char_start; x<=char_stop; x++) {
      if ( ends[x] - starts[x] > SWITCH_TO_SHELL) {
	radixify(&buffer[starts[x] * record_size], 
		 ends[x] - starts[x], 
		 digit+1,
		 char_start,
		 char_stop,
		 record_size,
		 key_size, 
		 stack_size,
		 cut_off);
      } else {
	if (ends[x] - starts[x] <= 1) continue;
	shellsort(&buffer[starts[x] * record_size], ends[x] - starts[x], record_size, key_size);
      }
    }
  } else {
    for(x=char_start; x<=char_stop; x++)
      if (ends[x] - starts[x] > 1)
	{
	  shellsort(&buffer[starts[x] * record_size], ends[x] - starts[x], record_size, key_size);
	}
  }
}


int open_sort(char *path, struct sort *sort) {
  void *buffer = NULL;

  int fd = open(path, O_RDWR);
  if (fd == -1)
    goto error;

  struct stat stats;
  if (-1 == fstat(fd, &stats))
    goto error; 
  if (!(buffer = mmap(NULL, 
		      stats.st_size,
		      PROT_READ | PROT_WRITE,
		      MAP_SHARED,
		      fd,
		      0
		      )))
    goto error;
  

  
  sort->buffer = buffer;
  sort->size = stats.st_size;
  sort->fd = fd;
  return 0;

 error:
  perror(path);
  if (buffer)
    munmap(buffer, stats.st_size);
  if (fd != -1)
    close(fd);
  sort->buffer = 0;
  sort->fd = 0;
  return -1;
}
  

void close_sort(struct sort *sort) {
  if (sort->buffer) {
    munmap(sort->buffer, sort->size);
    sort->buffer = 0;
    sort->size = 0;
  }
  
  if (sort->fd) {
    close(sort->fd);
    sort->fd = 0;
  }
}


int
main(int argc, char *argv[])
{
  int opt;
  int char_start = 0;
  int char_stop = 255;
  int record_size=100;
  int key_size=10;
  int stack_size=12;
  int cut_off = 4;
  verbosity = 0;
  while ((opt = getopt(argc, argv, "var:k:s:c:")) != -1) {
    switch (opt) {
    case 'v':
      verbosity += 1;
      break;
    case 'a':
      char_start = 32;
      char_stop = 128;
      break;
    case 'r':
      record_size = atoi(optarg);
      break;
    case 'k':
      key_size = atoi(optarg);
      break;
    case 's':
      stack_size = atoi(optarg);
      break;
    case 'c':
      cut_off = atoi(optarg);
    default:
      fprintf(stderr, "Invalid parameter: -%c\n", opt);
      goto failure;
    }
  }

  fprintf(stderr, "foo\n");
  if (optind >= argc) {    
    fprintf(stderr, "Expected argument after options\n");
    goto failure;
  }

  while(optind < argc) {
    printf("Opening %s\n", argv[optind]);
    struct sort sort; 
    if (-1==open_sort(argv[optind], &sort))
      goto failure;

    radixify(sort.buffer, 
	     sort.size / record_size,
	     0,
	     char_start,
	     char_stop, 
	     key_size, 
	     record_size, 
	     stack_size, 
	     cut_off);
    close_sort(&sort);
    optind++;
  }

  exit(0);
 failure:
  fprintf(stderr, 
	  "Usage: %s [-v] [-a] [-r ###] [-k ###] [-s ###] file1, file2 ... \n",
	  argv[0]);
  fprintf(stderr,
	  "Individually sort binary files inplace.\n"
	  "\n"
	  "Sorting Options:\n"
	  "\n"
	  "  -a       assume files are printable 7-bit ascii instead of binary\n"
          "  -k ###   size of compariable section of record, in bytes (default 100)\n"
	  "  -r ###   size of overall record, in bytes.  (default 100)\n"
	  "\n"
	  "Options:\n"
	  "  -v  verbose output logging\n"
	  "\n"
	  "Tuning Options:\n"
	  "\n"
	  "  -s ###   pushahead stack size.  (default 12)\n"
	  "  -c ###   recursion limit after which to use shell sort (defaults to 4)\n" 
	  "\n"
	  "Report bsort bugs to adam@pelotoncycle.com\n"
	  );
  exit(1);
}




