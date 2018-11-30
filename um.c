#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <except.h>

#include "except.h"
#include "assert.h"

extern Except_T Bitpack_Overflow;
Except_T Bitpack_Overflow = { "Overflow packing bits" };

typedef struct memory {
        uint32_t** segments;
        uint32_t memlength;
} *memory;

typedef struct unmapped_list {
        uint32_t *identifiers;
        uint32_t lastindex;
        uint32_t listlength;
} *unmapped_list;

static inline void run_prog(memory mem, unmapped_list unmapped, uint32_t registers[], 
              uint32_t *prog_count);

static inline memory init_mem();
static inline void init_prog(memory mem, FILE *fp, uint32_t num_words);
static inline unmapped_list init_unmapped_list();
static inline uint32_t get_word(memory mem, unsigned seg_num, unsigned offset);
static inline void put_word(memory mem, unsigned seg_num, unsigned offset, uint32_t val);
static inline void free_mem(memory mem);
static inline void free_unmapped_list(unmapped_list unmapped);

static inline void initialize_regs(uint32_t registers[]);
static inline uint32_t at_reg(uint32_t registers[], unsigned index);
static inline void update_reg(uint32_t registers[], unsigned index, uint32_t word);

static inline void decode_word(uint32_t word, uint32_t *opcode, unsigned *a, unsigned *b, 
                 unsigned *c, unsigned *lvalue);
static inline void conditional_move(uint32_t registers[], unsigned a, unsigned b, unsigned c);
static inline void segmented_load(uint32_t registers[], memory mem, unsigned a, unsigned b, 
                    unsigned c);
static inline void segmented_store(uint32_t registers[], memory mem, unsigned a, unsigned b, 
                     unsigned c);
static inline void addition(uint32_t registers[], unsigned a, unsigned b, unsigned c);
static inline void multiplication (uint32_t registers[], unsigned a, unsigned b, unsigned c);
static inline void division(uint32_t registers[], unsigned a, unsigned b, unsigned c);
static inline void bitwise_NAND(uint32_t registers[], unsigned a, unsigned b, unsigned c);
static inline void halt(memory mem, uint32_t *prog_count);
static inline void map_segment(uint32_t registers[], memory mem, unmapped_list unmapped, unsigned b, 
                 unsigned c);
static inline void unmap_segment(uint32_t registers[], memory mem, unmapped_list unmapped, 
                   unsigned c);
static inline void output(uint32_t registers[], unsigned c);
static inline void input(uint32_t registers[], unsigned c);
static inline void load_program(memory mem, uint32_t registers[], uint32_t *prog_count, 
                  unsigned b, unsigned c);
static inline void load_value(uint32_t registers[], unsigned a, unsigned lvalue);

static inline uint32_t io_input();
static inline void io_output(uint32_t word);


static inline bool Bitpack_fitsu(uint64_t n, unsigned width);
static inline bool Bitpack_fitss( int64_t n, unsigned width);
static inline uint64_t Bitpack_getu(uint64_t word, unsigned width, unsigned lsb);
static inline int64_t Bitpack_gets(uint64_t word, unsigned width, unsigned lsb);
static inline uint64_t Bitpack_newu(uint64_t word, unsigned width, unsigned lsb, uint64_t value);
static inline uint64_t Bitpack_news(uint64_t word, unsigned width, unsigned lsb, int64_t value);


int main(int argc, char *argv[]) {
        if (argc == 1) {
                fprintf(stdout, "Error: A UM file not provided\n");
                exit(EXIT_FAILURE);
        }

        FILE *fp;
        fp = fopen(argv[1], "r");

        /* Checks if the file is read */
        if (fp == NULL) {
                fprintf(stderr, "%s: %s %s %s\n",
                        argv[0], "Could not open file ",
                        argv[1], "for reading");
                exit(EXIT_FAILURE);
        }

        /* Main UM components */
        memory mem;
        unmapped_list unmapped;
        uint32_t registers[8];
        uint32_t prog_count = 0;

        struct stat sb;
        stat(argv[1], &sb);
        uint32_t num_words = sb.st_size / 4;

        /* Initializes main UM components */
        initialize_regs(registers);
        unmapped = init_unmapped_list();
        mem = init_mem();

        init_prog(mem, fp, num_words);

        /* Runs the UM */
        run_prog(mem, unmapped, registers, &prog_count);

        /* Frees memory */
        fclose(fp);
        free_mem(mem);
        free_unmapped_list(unmapped);

        exit(EXIT_SUCCESS);
}

/* Function: run_program
 * Does: Runs all instructions
 * Paramters: Seq_T, Seq_T, UArray_T, uint32_t*
 * Returns: none
 */
static inline void run_prog(memory mem, unmapped_list unmapped, uint32_t registers[], 
              uint32_t *prog_count) {
        bool prog_change = false;

        uint32_t curr_length = mem->segments[0][1];

        /* Keeps running until the program counter points to the last 
         * instruction 
         */
        while (true) {
                uint32_t instruction = get_word(mem, 0, *prog_count);
                uint32_t opcode;
                unsigned a, b, c, lvalue;
                decode_word(instruction, &opcode, &a, &b, &c, &lvalue);

                *prog_count = *prog_count + 1;

                // fprintf(stderr, "%u\n", opcode);

                /* Executes the specified instruction */
                switch (opcode) {
                        case 0 :
                                conditional_move(registers, a, b, c);
                                break;
                        case 1 :
                                segmented_load(registers, mem, a, b, c);
                                break;
                        case 2 :
                                segmented_store(registers, mem, a, b, c);
                                break;
                        case 3 :
                                addition(registers, a, b, c);
                                break;
                        case 4 :
                                multiplication(registers, a, b, c);
                                break;
                        case 5 :
                                division(registers, a, b, c);
                                break;
                        case 6 :
                                bitwise_NAND(registers, a, b, c);
                                break;
                        case 7 :
                                halt(mem, prog_count);
                                break;
                        case 8 :
                                map_segment(registers, mem, unmapped, b, c);
                                break;
                        case 9 :
                                unmap_segment(registers, mem, unmapped, c);
                                break;
                        case 10 :
                                output(registers, c);
                                break;
                        case 11 :
                                input(registers, c);
                                break;
                        case 12 :
                                load_program(mem, registers, prog_count, b, c);
                                prog_change = true;
                                break;
                        case 13 :
                                load_value(registers, a, lvalue);
                                break;
                        default:
                                // fprintf(stderr, "%u\n", opcode);
                                // fprintf(stderr, "Error: Invalid Instruction\n");
                                exit(EXIT_FAILURE);

                }

                if (prog_change) {
                        curr_length = mem->segments[0][1];
                        prog_change = false;
                }
                
                /* Check if the last instruction has been executed*/
                if (*prog_count == curr_length) {
                        return;
                } 
        }
}

static inline memory init_mem() 
{
        memory mem = malloc(sizeof(* mem));
        mem->memlength = 1;
        mem->segments = malloc(sizeof(uint32_t*));

        return mem;
}

static inline void init_prog(memory mem, FILE *fp, uint32_t num_words)
{
        int reader = 1;
        int index = 2;
        uint32_t temp_ch;
        uint32_t word = 0;
        bool end = false;

        if (mem == NULL || fp == NULL) {
                fprintf(stdout, "Error: Memory/File pointer is uninitialized");
                exit(EXIT_FAILURE);
        }

        mem->segments[0] = malloc(sizeof(uint32_t) * (num_words + 2));
        mem->segments[0][0] = 1;
        mem->segments[0][1] = 0;

        end = false;

        rewind(fp);

        while (!end) {
                temp_ch = fgetc(fp);

                if ((int)temp_ch == EOF) {
                        end = true;
                } else {
                        word = (uint32_t)Bitpack_newu((uint64_t)word, 8, 
                        (32 - (8 * reader)), temp_ch);

                        if (reader < 4) {
                                reader++;
                        } else {
                                mem->segments[0][index] = word;

                                reader = 1;
                                index++;
                        }
                }
        }

}

static inline unmapped_list init_unmapped_list()
{
        unmapped_list unmapped = malloc(sizeof(*unmapped));
        unmapped->identifiers = calloc(100, sizeof(uint32_t));
        unmapped->lastindex = 0;
        unmapped->listlength = 100;

        return unmapped;
}

static inline uint32_t get_word(memory mem, unsigned seg_num, unsigned offset)
{
        if (mem == NULL) {
                fprintf(stdout, "Error: Memory is uninitialized");
                exit(EXIT_FAILURE);
        }

        uint32_t word = mem->segments[seg_num][offset + 2];

        return word;

}

static inline void put_word(memory mem, unsigned seg_num, unsigned offset, uint32_t val)
{
        if (mem == NULL) {
                fprintf(stdout, "Error: Memory is uninitialized");
                exit(EXIT_FAILURE);
        }

        mem->segments[seg_num][offset + 2] = val;
}

static inline void free_mem(memory mem)
{
        if (mem == NULL) {
                fprintf(stdout, "Error: Memory is uninitialized");
                exit(EXIT_FAILURE);
        }

        int length = mem->memlength;

        /* Frees each mem_seg struct*/
        for (int i = 0; i < length; i++) {
                if (mem->segments[i] != NULL)
                        free(mem->segments[i]);
        }

        free(mem->segments);

        free(mem);
}

static inline void free_unmapped_list(unmapped_list unmapped)
{

        if (unmapped->identifiers != NULL) {
                free(unmapped->identifiers);
        }

        free(unmapped);
}

/* Function: initialize_regs
 * Does: Initializes registers
 * Paramters: None
 * Returns: UArray_T
 */
static inline void initialize_regs(uint32_t registers[])
{
        /* Sets the values of all registers to 0 */
        for (int i = 0; i < 8; i++) {
                registers[i] = 0;
        }
}

/* Function: free_regs
 * Does: Frees registers
 * Paramters: UArray_T
 * Returns: None
 */
static inline uint32_t at_reg(uint32_t registers[], unsigned index)
{    
        if (index > 7) {
                fprintf(stdout, "Error: Invalid register index provided");
                exit(EXIT_FAILURE);
        }

        return registers[index];
}

/* Function: update_reg
 * Does: Updates a specified register to a specified value
 * Paramters: UArray_T, unsigned, uint32_t
 * Returns: None
 */
static inline void update_reg(uint32_t registers[], unsigned index, uint32_t word)
{
        if (index > 7) {
                fprintf(stdout, "Error: Invalid register index provided");
                exit(EXIT_FAILURE);
        }

        registers[index] = word;
}

/* Function: decode_word
 * Does: Decodes a given word and puts the appropriate bits into
         appropriate parameters
 * Paramters: uint32_t, uint32_t*, unsigned*, unsigned*, unsigned*, unsigned*
 * Returns: None
 */
static inline void decode_word(uint32_t word, uint32_t *opcode, unsigned *a, unsigned *b, 
                 unsigned *c, unsigned *lvalue)
{
        *opcode = (uint32_t)Bitpack_getu(word, 4, 28);

        if (*opcode == 13) {
                *a = (unsigned)Bitpack_getu(word, 3, 25);
                *b = 0;
                *c = 0;
                *lvalue = (unsigned)Bitpack_getu(word, 25, 0);
        } else {
                *a = (unsigned)Bitpack_getu(word, 3, 6);
                *b = (unsigned)Bitpack_getu(word, 3, 3);
                *c = (unsigned)Bitpack_getu(word, 3, 0);
                *lvalue = 0;
        }
}

/* Function: conditional move
 * Does: Performs a conditional move
 * Paramters: UArray_T, unsigned, unsigned, unsigned
 * Returns: None
 */
static inline void conditional_move(uint32_t registers[], unsigned a, unsigned b, unsigned c)
{
        if (a > 7 || b > 7 || c > 7) {
                fprintf(stdout, "Error: Invalid register index provided");
                exit(EXIT_FAILURE);
        }

        if (at_reg(registers, c) == 0) {
                return;
        } else {
                uint32_t temp = at_reg(registers, b);
                update_reg(registers, a, temp);
        }
}

/* Function: segmented_load
 * Does: Performs a segmented load
 * Paramters: UArray_T, Seq_T, unsigned, unsigned
 * Returns: None
 */
static inline void segmented_load(uint32_t registers[], memory mem, unsigned a, unsigned b, 
                    unsigned c)
{
        if (a > 7 || b > 7 || c > 7) {
                fprintf(stdout, "Error: Invalid register index provided");
                exit(EXIT_FAILURE);
        }

        if (mem == NULL) {
                fprintf(stdout, "Error: Memory not initialized");  
                exit(EXIT_FAILURE);
        }
        
        unsigned val_b = at_reg(registers, b);
        unsigned val_c = at_reg(registers, c);

        uint32_t new_word = get_word(mem, val_b, val_c);
        update_reg(registers, a, new_word); 
}

/* Function: segmented_store
 * Does: Performs a segmented store
 * Paramters: UArray_T, Seq_T, unsigned, unsigned
 * Returns: None
 */
static inline void segmented_store(uint32_t registers[], memory mem, unsigned a, unsigned b, 
                     unsigned c)
{
        if (a > 7 || b > 7 || c > 7) {
                fprintf(stdout, "Error: Invalid register index provided");
                exit(EXIT_FAILURE);
        }

        if (mem == NULL) {
                fprintf(stdout, "Error: Memory not initialized");  
                exit(EXIT_FAILURE);
        }
        
        unsigned val_a = at_reg(registers, a);
        unsigned val_b = at_reg(registers, b);
        unsigned val_c = at_reg(registers, c);

        put_word(mem, val_a, val_b, val_c);
}

/* Function: addition
 * Does: Performs an addition
 * Paramters: UArray_T, unsigned, unsigned, unsigned
 * Returns: None
 */
static inline void addition(uint32_t registers[], unsigned a, unsigned b, unsigned c)
{
        if (a > 7 || b > 7 || c > 7) {
                fprintf(stderr, "Error: Invalid register index provided");
                exit(EXIT_FAILURE);
        }

        uint32_t first = at_reg(registers, b);
        uint32_t second = at_reg(registers, c);
        uint32_t sum = (first + second) % 4294967296;

        update_reg(registers, a, sum);
}

/* Function: multiplication
 * Does: Performs a multiplication
 * Paramters: UArray_T, unsigned, unsigned, unsigned
 * Returns: None
 */
static inline void multiplication (uint32_t registers[], unsigned a, unsigned b, unsigned c)
{
        if (a > 7 || b > 7 || c > 7) {
                fprintf(stdout, "Error: Invalid register index provided");
                exit(EXIT_FAILURE);
        }

        uint32_t first = at_reg(registers, b);
        uint32_t second = at_reg(registers, c);
        uint32_t product = (first * second) % 4294967296;

        update_reg(registers, a, product);
}

/* Function: division
 * Does: Performs a division
 * Paramters: UArray_T, unsigned, unsigned, unsigned
 * Returns: None
 */
static inline void division(uint32_t registers[], unsigned a, unsigned b, unsigned c)
{
        if (a > 7 || b > 7 || c > 7) {
                fprintf(stdout, "Error: Invalid register index provided");
                exit(EXIT_FAILURE);
        }

        uint32_t first = at_reg(registers, b);
        uint32_t second = at_reg(registers, c);
        uint32_t quotient = (first / second) % 4294967296;

        update_reg(registers, a, quotient);
}

/* Function: bitwise_NAND
 * Does: Performs a bitwise NAND
 * Paramters: UArray_T, unsigned, unsigned, unsigned
 * Returns: None
 */
static inline void bitwise_NAND(uint32_t registers[], unsigned a, unsigned b, unsigned c)
{
        if (a > 7 || b > 7 || c > 7) {
                fprintf(stdout, "Error: Invalid register index provided");
                exit(EXIT_FAILURE);
        }

        uint32_t first = at_reg(registers, b);
        uint32_t second = at_reg(registers, c);
        uint32_t result = first & second;
        result = ~result;
        update_reg(registers, a, result);
}

/* Function: halt
 * Does: Halts the program
 * Paramters: Seq_T, uint32_t*
 * Returns: None
 */
static inline void halt(memory mem, uint32_t *prog_count)
{
        if (mem == NULL) {
                fprintf(stdout, "Error: Memory not initialized"); 
                exit(EXIT_FAILURE); 
        }
        
        *prog_count = mem->segments[0][1];
}

/* Function: map_segment
 * Does: Maps a segment with a specified number of words
 * Paramters: UArray_T, Seq_T, Seq_T, unsigned, unsigned
 * Returns: None
 */
static inline void map_segment(uint32_t registers[], memory mem, unmapped_list unmapped, unsigned b, 
                 unsigned c)
{
        if (b > 7 || c > 7) {
                fprintf(stdout, "Error: Invalid register index provided");
                exit(EXIT_FAILURE);
        }

        if (mem == NULL || unmapped == NULL) {
                fprintf(stdout, "Error: Memory not initialized");  
                exit(EXIT_FAILURE);
        }
        
        unsigned num_words = at_reg(registers, c);
        uint32_t curr_memsize = mem->memlength;
        // fprintf(stderr, "Memlength: %u\n", curr_memsize);  
        uint32_t new_index;

        /* Checks if there are any unmapped segments */
        if (unmapped->lastindex != 0) {
                /* Gets the segment number of an unmapped segment */
                new_index = unmapped->identifiers[unmapped->lastindex];
                (unmapped->lastindex)--;

                mem->segments[new_index] = realloc(mem->segments[new_index], sizeof(uint32_t) * (num_words + 2));
                mem->segments[new_index][0] = 1;
                mem->segments[new_index][1] = num_words;

        } else {
                /* Creates a new struct */
                size_t newsize = (curr_memsize + 1) * sizeof(uint32_t*);
                mem->segments = realloc(mem->segments, newsize);
                (mem->memlength)++;

                new_index = curr_memsize;

                mem->segments[new_index] = malloc(sizeof(uint32_t) * (num_words + 2));
                mem->segments[new_index][0] = 1;
                mem->segments[new_index][1] = num_words;
        }

        /* Sets all words to 0 */
        for (unsigned i = 0; i < num_words; i++) {
                mem->segments[new_index][i + 2] = 0;
        }

        update_reg(registers, b, new_index);
}

/* Function: unmap_segment
 * Does: Maps the segment at a specified index
 * Paramters: UArray_T, Seq_T, Seq_T, unsigned
 * Returns: None
 */
static inline void unmap_segment(uint32_t registers[], memory mem, unmapped_list unmapped, 
                   unsigned c)
{
        if (c > 7) {
                fprintf(stdout, "Error: Invalid register index provided");
                exit(EXIT_FAILURE);
        }

        if (mem == NULL || unmapped == NULL) {
                fprintf(stdout, "Error: Memory not initialized");  
                exit(EXIT_FAILURE);
        }
        
        unsigned index = (unsigned)at_reg(registers, c);

        if (index == 0) {
                fprintf(stdout, "Error: Cannot unmap segment 0");  
                exit(EXIT_FAILURE);
        }

        /* Checks if the segment is already unmapped*/
        if (mem->segments[index][0] != 0) {
                free(mem->segments[index]);
                mem->segments[index] = NULL;
        } else {
                fprintf(stdout, "Error: Unmapping an unmapped segment");
                exit(EXIT_FAILURE);
        }

        if (unmapped->lastindex == (unmapped->listlength - 1)) {
                size_t newsize = sizeof(uint32_t) * (unmapped->listlength * 2);
                unmapped->identifiers = realloc(unmapped->identifiers, newsize);
                unmapped->listlength = unmapped->listlength * 2;
        } 
        
        (unmapped->lastindex)++;
        unmapped->identifiers[unmapped->lastindex] = index;
        
}

/* Function: output
 * Does: Outputs the value at the given register
 * Paramters: UArray_T, unsigned
 * Returns: None
 */
static inline void output(uint32_t registers[], unsigned c)
{
        if (c > 7) {
                fprintf(stdout, "Error: Invalid register index provided");
                exit(EXIT_FAILURE);
        }

        io_output(at_reg(registers, c));
}

/* Function: input
 * Does: Takes input and stores it in the given register
 * Paramters: UArray_T, unsigned
 * Returns: None
 */
static inline void input(uint32_t registers[], unsigned c)
{
        if (c > 7) {
                fprintf(stdout, "Error: Invalid register index provided");
                exit(EXIT_FAILURE);
        }

        uint32_t userinput = io_input();
        if (userinput == (unsigned)EOF) {
                userinput = ~0;      
        } 

        update_reg(registers, c, userinput);
}

/* Function: load_program
 * Does: Loads the sgment at the spcified index into the program
 * Paramters: Seq_T, UArray_T, uint32_t*, unsigned, unsigned
 * Returns: None
 */
static inline void load_program(memory mem, uint32_t registers[], uint32_t *prog_count, 
                  unsigned b, unsigned c)
{
        if (b > 7 || c > 7) {
                fprintf(stdout, "Error: Invalid register index provided");
                exit(EXIT_FAILURE);
        }

        if (mem == NULL) {
                fprintf(stdout, "Error: Memory not initialized");  
                exit(EXIT_FAILURE);
        }
        
        uint32_t seg_num = at_reg(registers, b);

        if (seg_num == 0) {
                *prog_count = at_reg(registers, c);
                return;
        }

        /* Makes a deep copy of the segment to be duplicated*/

        int length = mem->segments[seg_num][1];
        uint32_t* duplicate = malloc((length + 2) * sizeof(uint32_t));

        /* Copies each word */
        for (int i = 0; i < length + 2; i++) {
                duplicate[i] = mem->segments[seg_num][i];
        }

        /* Abandons the original program segment */
        free(mem->segments[0]);

        mem->segments[0] = duplicate;

        *prog_count = at_reg(registers, c);
}

/* Function: load_value
 * Does: Loadsa the given value to the given register
 * Paramters: UArray_T, unsigned, unsigned
 * Returns: None
 */
static inline void load_value(uint32_t registers[], unsigned a, unsigned lvalue)
{
        if (a > 7) {
                fprintf(stderr, "Error: Invalid register index provided");
                exit(EXIT_FAILURE);
        }

        update_reg(registers, a, lvalue);
}


static inline uint32_t io_input()
{
        char value = fgetc(stdin);
        
        return value;
}

static inline void io_output(uint32_t word)
{
        fputc(word, stdout);
}











static inline uint64_t shl(uint64_t word, unsigned bits)
{
        assert(bits <= 64);
        if (bits == 64)
                return 0;
        else
                return word << bits;
}

/*
 * shift R logical
 */
static inline uint64_t shr(uint64_t word, unsigned bits)
{
        assert(bits <= 64);
        if (bits == 64)
                return 0;
        else
                return word >> bits;
}

/*
 * shift R arith
 */
static inline int64_t sra(uint64_t word, unsigned bits)
{
        assert(bits <= 64);
        if (bits == 64)
                bits = 63; /* will get all copies of sign bit, 
                            * which is correct for 64
                            */
        /* Warning: following uses non-portable >> on
           signed value...see K&R 2nd edition page 206. */
        return ((int64_t) word) >> bits; 
}

/****************************************************************/
static inline bool Bitpack_fitss( int64_t n, unsigned width)
{
        if (width >= 64)
                return true;
        int64_t narrow = sra(shl(n, 64 - width),
                             64 - width); 
        return narrow == n;
}

static inline bool Bitpack_fitsu(uint64_t n, unsigned width)
{
        if (width >= 64)
                return true;
        /* thanks to Jai Karve and John Bryan  */
        /* clever shortcut instead of 2 shifts */
        return shr(n, width) == 0; 
}

/****************************************************************/

static inline int64_t Bitpack_gets(uint64_t word, unsigned width, unsigned lsb)
{
        if (width == 0) return 0;    /* avoid capturing unknown sign bit    */

        unsigned hi = lsb + width; /* one beyond the most significant bit */
        assert(hi <= 64);
        return sra(shl(word, 64 - hi),
                   64 - width);
}

static inline uint64_t Bitpack_getu(uint64_t word, unsigned width, unsigned lsb)
{
        unsigned hi = lsb + width; /* one beyond the most significant bit */
        assert(hi <= 64);
        /* different type of right shift */
        return shr(shl(word, 64 - hi),
                   64 - width); 
}

/****************************************************************/
static inline uint64_t Bitpack_newu(uint64_t word, unsigned width, unsigned lsb,
                      uint64_t value)
{
        unsigned hi = lsb + width; /* one beyond the most significant bit */
        assert(hi <= 64);
        if (!Bitpack_fitsu(value, width))
                RAISE(Bitpack_Overflow);
        return shl(shr(word, hi), hi)                 /* high part */
                | shr(shl(word, 64 - lsb), 64 - lsb)  /* low part  */
                | (value << lsb);                     /* new part  */
}

static inline uint64_t Bitpack_news(uint64_t word, unsigned width, unsigned lsb,
                      int64_t value)
{
        if (!Bitpack_fitss(value, width))
                RAISE(Bitpack_Overflow);
        /* thanks to Michael Sackman and Gilad Gray */
        return Bitpack_newu(word, width, lsb, Bitpack_getu(value, width, 0));
}