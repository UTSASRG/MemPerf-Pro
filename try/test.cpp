#include<stdio.h>
#include<stdlib.h>

#include "memsample.h"

int main() {
  int i, j; 
  initPMU();

 // fprintf(stderr, "i address %p j %p\n",&i, &j);
  for(i = 0; i < 100000; i++) {
     for(j = 0; j < 20000; j++) {
    //  printf("j is %d\n", j);
     }
  }
}


