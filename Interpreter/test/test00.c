extern int GET();
extern void * MALLOC(int);
extern void FREE(void *);
extern void PRINT(int);

int b = 10;
int c = b + 10;
int main() {
   int a;
   a=100;
   PRINT(b);
   PRINT(c); 
   PRINT(a);
}
