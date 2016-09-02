#include <stdio.h>
#include <string.h>


void gen_next(char* p,int len,int* next);
int kmp_match(char* text, char* pattern);


/*
 *	To search how many words in the text can match the pattern. Use KMP algorithm. 
 */ 
int kmp_match(char* text, char* pattern){
	char* cur_head;
	char* t;
	char* p;
	int matched_len;
	int pattern_len;
	int matched_times;
	int next[strlen(pattern)];

	if(!text){
		printf("No text or the text length is smaller than 0.\n");
		return -1;
	}

	if(!pattern){
		printf("No pattern.\n");
		return -1;
	}
	
	t=cur_head=text;
	p=pattern;
	
	matched_len=0;
	pattern_len=strlen(pattern);
	matched_times=0;

	memset(next,0,pattern_len*sizeof(int));
	gen_next(pattern,pattern_len,next);

	//Do KMP search. To know more about the algrothm, please refer to Google. 	
	while(*t){
		if(*t!=*p){
			if(matched_len>0){
				cur_head=cur_head+(matched_len-next[matched_len-1]);
				t=cur_head+next[matched_len-1];
				p=pattern+next[matched_len-1];
				matched_len=next[matched_len-1];
			}else if(matched_len==0){
				cur_head++;
				t=cur_head;
				p=pattern;
			}
		}

		else{
			p++;
			t++;
			matched_len++;
				
			if(matched_len==pattern_len){
				matched_times++;
				cur_head=cur_head+(matched_len-next[matched_len-1]);
				t=cur_head+next[matched_len-1];
				p=pattern+next[matched_len-1];
				matched_len=next[matched_len-1];
			}	
		}
	}
	return matched_times;
}


/*
 * Generize the next matrix used in KMP.
 */
void gen_next(char* p,int len,int* next){
	memset(next,0,len*sizeof(int));	

	if(len<=1){
		return;
	}

	int val=0;
	int i=0;
	for(i=1;i<len;i++){
		if(p[i]==p[val]){
			val++;
			next[i]=val;
		}
		else{
			val=0;
		}
	}
}


int main(int argc,char* argv[]){
	FILE* fin;
	FILE* fout;
	char text[100];
	char* read_begin;
	char* path_in=argv[1];
	char* pattern=argv[2];
	char* path_out=argv[3];
	char result[100];
	int cnt;
	int read_num;
	int matched_times;
	int left_word;
	
	//Check the argumentnumber, should be four.
	if(argc!=4){
		printf("The argument number should be four, like \"count <input-filename> <search-string> <output-filename>\"\n");
		return -1;
	}

	if((fin=fopen(path_in,"r"))==0){
		printf("No <input-filename> called \"%s\" be found. Please check the name.\n",argv[1]);
		return -1;
	}

	cnt=0;
	matched_times=0;
	left_word=0;
	read_begin=text;

	//Read the input-file into 'text', and then do search, until the end of the file.
	while(1){
		read_begin=text+left_word;
		memset(read_begin,'\0',sizeof(text)-left_word);

		read_num=fread(read_begin,sizeof(char),sizeof(text)-left_word-1,fin);
		
		if(read_num<=0){
			break;
		}
		
		cnt+=read_num;

		matched_times+=kmp_match(text,pattern);

		if((left_word+read_num)<(strlen(pattern)-1)){
			break;
		}else{
			int left=strlen(pattern)-1;
			strncpy(text,&text[read_num+left_word-left],left);
			left_word=left;
		}
	}
	
	//Write the result into the output file.
	memset(result,'\0',sizeof(result));
	sprintf(result,"file size: %d,\tmatched times: %d.\n",cnt,matched_times);
	
	if((fout=fopen(path_out,"w"))==0){
		printf("Error happened when trying to open or create the <output-file> %s.\n",\
						path_out);
	}
	
	if((fwrite(result,sizeof(char),strlen(result),fout))!=strlen(result)){
		printf("Error in write the answer to output file\n");
	}	
	return 0;
}
