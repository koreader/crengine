typedef struct {
   unsigned char ch1;
   unsigned char ch2;
   short    count;
} dbl_char_stat_t;

typedef struct {
   unsigned char ch1;
   unsigned char ch2;
   int      count;
} dbl_char_stat_long_t;

typedef struct {
   const short * ch_stat;
   const dbl_char_stat_t * dbl_ch_stat;
   const char * cp_name;
   const char * lang_name;
} cp_stat_t;
