#include "stdio.h"
#include "stdlib.h"
#include "time.h"

/*
$1: number of nodes
$2: number of domains
$3: number of gateways in a domain
$4: number of traffics
*/

//hard coding for dsts in AODV domain
	
int main(int argc, char **argv){
	
	/*	
	int nn = atoi(argv[1]);	
	int nd = atoi(argv[2]);
	int ng = atoi(argv[3]);
	int nt = atoi(argv[4]);
	*/
	
	//ping from every gt to dst
	//assume: 0~gt-1: gateways in domain A
	int dst1 = 41;
	int dst2 = 43;
	int dst3 = 45;
	int dst4 = 46; 
	
	//int index = 50;
//	int ndd = nn/nd;
	
	int start = 48;
	
	int index = 10;

	for(int i=0; i<25	; i++){

		if(i == dst1 ) continue;
		
		printf("set traffic(%d,src)\t\t\t\t%d ; \t\t", index, i);
		printf("set traffic(%d,dst)\t\t\t\t%d ;\n", index, dst1);
		printf("set traffic(%d,pksize)\t\t\t\t%d ;\n", index, 64);
		printf("set traffic(%d,rate)\t\t\t\t%dKb ;\n", index, 1);			//1Kbps
		printf("set traffic(%d,startoffset)\t\t%f ;\n", index, 5.0);	
		printf("\n");
	
		index ++;				
	}

	for(int i=0; i<25	; i++){

		if(i == dst1 ) continue;
		
		printf("set traffic(%d,src)\t\t\t\t%d ; \t\t", index, i);
		printf("set traffic(%d,dst)\t\t\t\t%d ;\n", index, dst2);
		printf("set traffic(%d,pksize)\t\t\t\t%d ;\n", index, 64);
		printf("set traffic(%d,rate)\t\t\t\t%dKb ;\n", index, 1);			//1Kbps
		printf("set traffic(%d,startoffset)\t\t%f ;\n", index, 5.0);	
		printf("\n");
	
		index ++;				
	}

	for(int i=0; i<25	; i++){

		if(i == dst1 ) continue;
		
		printf("set traffic(%d,src)\t\t\t\t%d ; \t\t", index, i);
		printf("set traffic(%d,dst)\t\t\t\t%d ;\n", index, dst3);
		printf("set traffic(%d,pksize)\t\t\t\t%d ;\n", index, 64);
		printf("set traffic(%d,rate)\t\t\t\t%dKb ;\n", index, 1);			//1Kbps
		printf("set traffic(%d,startoffset)\t\t%f ;\n", index, 5.0);	
		printf("\n");
	
		index ++;				
	}

	for(int i=0; i<25	; i++){

		if(i == dst1 ) continue;
		
		printf("set traffic(%d,src)\t\t\t\t%d ; \t\t", index, i);
		printf("set traffic(%d,dst)\t\t\t\t%d ;\n", index, dst4);
		printf("set traffic(%d,pksize)\t\t\t\t%d ;\n", index, 64);
		printf("set traffic(%d,rate)\t\t\t\t%dKb ;\n", index, 1);			//1Kbps
		printf("set traffic(%d,startoffset)\t\t%f ;\n", index, 5.0);	
		printf("\n");
	
		index ++;				
	}
	
	/*
	for(int i=0; i<ng; i++){
		
		index ++;
		
		printf("set traffic(%d,src)\t\t\t\t%d ; \t\t", index, i);
		printf("set traffic(%d,dst)\t\t\t\t%d ;\n", index, dst2);
		printf("set traffic(%d,pksize)\t\t\t\t%d ;\n", index, 64);
		printf("set traffic(%d,rate)\t\t\t\t%dKb ;\n", index, 1);			//1Kbps
		printf("set traffic(%d,startoffset)\t\t%f ;\n", index, 5.0);	
		printf("\n");

	}
	
	for(int i=0; i<ng; i++){
		
		index ++;
		
		printf("set traffic(%d,src)\t\t\t\t%d ; \t\t", index, i);
		printf("set traffic(%d,dst)\t\t\t\t%d ;\n", index, dst3);
		printf("set traffic(%d,pksize)\t\t\t\t%d ;\n", index, 64);
		printf("set traffic(%d,rate)\t\t\t\t%dKb ;\n", index, 1);			//1Kbps
		printf("set traffic(%d,startoffset)\t\t%f ;\n", index, 5.0);	
		printf("\n");

	}
	
	for(int i=0; i<ng; i++){
		
		index ++;
		
		printf("set traffic(%d,src)\t\t\t\t%d ; \t\t", index, i);
		printf("set traffic(%d,dst)\t\t\t\t%d ;\n", index, dst4);
		printf("set traffic(%d,pksize)\t\t\t\t%d ;\n", index, 64);
		printf("set traffic(%d,rate)\t\t\t\t%dKb ;\n", index, 1);			//1Kbps
		printf("set traffic(%d,startoffset)\t\t%f ;\n", index, 5.0);	
		printf("\n");

	}
	*/	

	//index++;
	
	printf("set npt\t\t\t%d\n", index);
	return 0;	
}
