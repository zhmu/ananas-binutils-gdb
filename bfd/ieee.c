/* bfd back-end for ieee-695 objects.

   IEEE 695 format is a stream of records, which we parse using a simple one-
   token (which is one byte in this lexicon) lookahead recursive decent
   parser.  */

/* Copyright (C) 1990, 1991 Free Software Foundation, Inc.

This file is part of BFD, the Binary File Diddler.

BFD is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 1, or (at your option)
any later version.

BFD is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with BFD; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include "sysdep.h"

#include "bfd.h"
#include "libbfd.h"
#include "ieee.h"
#include "libieee.h"

/* Functions for writing to ieee files in the strange way that the
   standard requires. */

static void
DEFUN(ieee_write_byte,(abfd, byte),
      bfd *abfd AND
      bfd_byte byte)
{
  bfd_write((PTR)&byte, 1, 1, abfd);
}


static void
DEFUN(ieee_write_2bytes,(abfd, bytes),
      bfd *abfd AND
      int bytes)
{
  bfd_byte buffer[2];
  buffer[0] = bytes >> 8;
  buffer[1] = bytes & 0xff;

  bfd_write((PTR)buffer, 1, 2, abfd);
}

static void
DEFUN(ieee_write_int,(abfd, value),
      bfd *abfd AND
      bfd_vma value)
{
  if (value >= 0 && value <= 127) {
    ieee_write_byte(abfd, value);
  }
  else {
    unsigned int length;
    /* How many significant bytes ? */
    /* FIXME FOR LONGER INTS */
    if (value & 0xff000000) {
      length = 4;
    }
    else if (value & 0x00ff0000) {
      length  = 3;
    }
    else if (value & 0x0000ff00) {
      length = 2;
    }
    else length = 1;

    ieee_write_byte(abfd, (int)ieee_number_repeat_start_enum + length);
    switch (length) {
    case 4:
      ieee_write_byte(abfd, value >> 24);
    case 3:
      ieee_write_byte(abfd, value >> 16);
    case 2:
      ieee_write_byte(abfd, value >> 8);
    case 1:
      ieee_write_byte(abfd, value);
    }
  }
}

static void
DEFUN(ieee_write_id,(abfd, id),
      bfd *abfd AND
      CONST char *id)
{
  size_t length = strlen(id);
  if (length >= 0 && length <= 127) {
    ieee_write_byte(abfd, length);
  }
  else if (length < 255) {
    ieee_write_byte(abfd, ieee_extension_length_1_enum);
    ieee_write_byte(abfd, length);
  }
  else if (length < 65535) {
    ieee_write_byte(abfd, ieee_extension_length_2_enum);
    ieee_write_byte(abfd, length >> 8);
    ieee_write_byte(abfd, length & 0xff);  
  }
  else {
    BFD_FAIL();
  }
  bfd_write((PTR)id, 1, length, abfd);
}
/***************************************************************************
Functions for reading from ieee files in the strange way that the
standard requires:
*/


#define this_byte(ieee) *(ieee->input_p)
#define next_byte(ieee) (ieee->input_p++)
#define this_byte_and_next(ieee) (*(ieee->input_p++))


static unsigned short 
DEFUN(read_2bytes,(ieee),
     ieee_data_type *ieee)
{
  unsigned  char c1 = this_byte_and_next(ieee);
  unsigned  char c2 = this_byte_and_next(ieee);
  return (c1<<8 ) | c2;

}

static void
DEFUN(bfd_get_string,(ieee, string, length),
    ieee_data_type *ieee AND
      char *string AND
      size_t length)
{
  size_t i;
  for (i= 0; i < length; i++) {
    string[i] = this_byte_and_next(ieee);
  }
}

static char *
DEFUN(read_id,(ieee),
    ieee_data_type *ieee)
{
  size_t length;
  char *string;
  length = this_byte_and_next(ieee);
  if (length >= 0x00 && length <= 0x7f) {
    /* Simple string of length 0 to 127 */
  }
  else if (length == 0xde) {
    /* Length is next byte, allowing 0..255 */
    length = this_byte_and_next(ieee);
  }
  else if (length == 0xdf) {
    /* Length is next two bytes, allowing 0..65535 */
    length = this_byte_and_next(ieee) ;
    length = (length * 256) + this_byte_and_next(ieee);
  }
  /* Buy memory and read string */
  string = bfd_alloc(ieee->abfd, length+1);
  bfd_get_string(ieee, string, length);
  string[length] = 0;
  return string;
}

static void
DEFUN(ieee_write_expression,(abfd, value, section, symbol, pcrel, index),
      bfd*abfd AND
      bfd_vma value AND
      asection *section AND
      asymbol *symbol AND
      boolean pcrel AND
			    unsigned int index)
{
  unsigned int plus_count = 0;
  ieee_write_int(abfd, value);
  if (section != (asection *)NULL) {
    plus_count++;
    ieee_write_byte(abfd, ieee_variable_L_enum);
    ieee_write_byte(abfd, section->index  +IEEE_SECTION_NUMBER_BASE);
  }



  if (symbol != (asymbol *)NULL) {
    plus_count++;
    if ((symbol->flags & BSF_UNDEFINED ) ||
	(symbol->flags & BSF_FORT_COMM)) {
      ieee_write_byte(abfd, ieee_variable_X_enum);
      ieee_write_int(abfd, symbol->value);
    }
    else if (symbol->flags & BSF_GLOBAL) {
      ieee_write_byte(abfd, ieee_variable_I_enum);
      ieee_write_int(abfd, symbol->value);
    }
    else if (symbol->flags & BSF_LOCAL) {
      /* This is a reference to a defined local symbol, 
	 We can easily do a local as a section+offset */
      if (symbol->section != (asection *)NULL) {
	/* If this symbol is not absolute, add the base of it */
	ieee_write_byte(abfd, ieee_variable_L_enum);
	ieee_write_byte(abfd, symbol->section->index +
			IEEE_SECTION_NUMBER_BASE);
	plus_count++;
      }
      
      ieee_write_int(abfd, symbol->value);
    }
    else {

      BFD_FAIL();
    }
  }

  if(pcrel) {
    /* subtract the pc from here by asking for PC of this section*/
    ieee_write_byte(abfd, ieee_variable_P_enum);
    ieee_write_byte(abfd, index  +IEEE_SECTION_NUMBER_BASE);
    ieee_write_byte(abfd, ieee_function_minus_enum);
  }

  while (plus_count > 0) {
    ieee_write_byte(abfd, ieee_function_plus_enum);
    plus_count--;
  }

}









/*****************************************************************************/

/*
writes any integer into the buffer supplied and always takes 5 bytes
*/
static void
DEFUN(ieee_write_int5,(buffer, value),
      bfd_byte*buffer AND
      bfd_vma value )
{
  buffer[0] = ieee_number_repeat_4_enum;
  buffer[1] = (value >> 24 ) & 0xff;
  buffer[2] = (value >> 16 ) & 0xff;
  buffer[3] = (value >> 8 ) & 0xff;
  buffer[4] = (value >> 0 ) & 0xff;
}
static void
DEFUN(ieee_write_int5_out, (abfd, value),
      bfd *abfd AND
      bfd_vma value)
{
  bfd_byte b[5];
  ieee_write_int5(b, value);
  bfd_write((PTR)b,1,5,abfd);
}


static boolean 
DEFUN(parse_int,(ieee, value_ptr),
      ieee_data_type  *ieee AND
      bfd_vma *value_ptr)
{
  int value = this_byte(ieee);
  int result;
  if (value >= 0 && value <= 127) {
    *value_ptr = value;
    next_byte(ieee);
    return true;
  } 
  else if (value >= 0x80 && value <= 0x88) {
    unsigned int count = value & 0xf;
    result = 0;
    next_byte(ieee);
    while (count) {
      result =(result << 8) | this_byte_and_next(ieee);
      count--;
    }
    *value_ptr = result;
    return true;
  } 
  return false;
}
static int
DEFUN(parse_i,(ieee, ok),
    ieee_data_type *ieee AND
      boolean *ok)
{
  bfd_vma x;
  *ok = parse_int(ieee, &x);
  return x;
}

static bfd_vma 
DEFUN(must_parse_int,(ieee),
      ieee_data_type *ieee)
{
  bfd_vma result;
  BFD_ASSERT(parse_int(ieee, &result) == true);
  return result;
}

typedef struct 
{
  bfd_vma value;
  asection *section;
  ieee_symbol_index_type symbol;
} ieee_value_type;


static 
reloc_howto_type abs32_howto 
 = HOWTO(1,0,2,32,0,0,0,true,0,"abs32",true,0xffffffff, 0xffffffff,false);
static
reloc_howto_type abs16_howto 
 = HOWTO(1,0,1,16,0,0,0,true,0,"abs16",true,0x0000ffff, 0x0000ffff,false);

static 
reloc_howto_type rel32_howto 
 = HOWTO(1,0,2,32,1,0,0,true,0,"rel32",true,0xffffffff, 0xffffffff,false);
static
reloc_howto_type rel16_howto 
 = HOWTO(1,0,1,16,1,0,0,true,0,"rel16",true,0x0000ffff, 0x0000ffff,false);

static ieee_symbol_index_type NOSYMBOL = {  0, 0};


static void
DEFUN(parse_expression,(ieee, value, section, symbol, pcrel, extra),
      ieee_data_type *ieee AND
      bfd_vma *value AND
      asection **section AND
      ieee_symbol_index_type *symbol AND
      boolean *pcrel AND
      unsigned int *extra)

{
#define POS sp[1]
#define TOS sp[0]
#define NOS sp[-1]
#define INC sp++;
#define DEC sp--;

  boolean loop = true;
  ieee_value_type stack[10];

  /* The stack pointer always points to the next unused location */
#define PUSH(x,y,z) TOS.symbol=x;TOS.section=y;TOS.value=z;INC;
#define POP(x,y,z) DEC;x=TOS.symbol;y=TOS.section;z=TOS.value;
  ieee_value_type *sp = stack;

  while (loop) {
    switch (this_byte(ieee)) 
	{
	case ieee_variable_P_enum:
	  /* P variable, current program counter for section n */
	    {
	      int section_n ;
	      next_byte(ieee);
	      *pcrel = true;
	      section_n  = must_parse_int(ieee);
	      PUSH(NOSYMBOL, 0,
		   TOS.value = ieee->section_table[section_n]->vma +
		   ieee_per_section(ieee->section_table[section_n])->pc);
	      break;
	    }
	case ieee_variable_L_enum:
	  /* L variable  address of section N */
	  next_byte(ieee);
	  PUSH(NOSYMBOL,ieee->section_table[must_parse_int(ieee)],0);
	  break;
	case ieee_variable_R_enum:
	  /* R variable, logical address of section module */
	  /* FIXME, this should be different to L */
	  next_byte(ieee);
	  PUSH(NOSYMBOL,ieee->section_table[must_parse_int(ieee)],0);
	  break;
	case ieee_variable_S_enum:
	  /* S variable, size in MAUS of section module */
	  next_byte(ieee);
	  PUSH(NOSYMBOL,
	       0,
	       ieee->section_table[must_parse_int(ieee)]->size);
	  break;

	case ieee_variable_X_enum:
	  /* Push the address of external variable n */
	    {
	      ieee_symbol_index_type sy;
	      next_byte(ieee);
	      sy.index  = (int)(must_parse_int(ieee)) ;
	      sy.letter = 'X';

	      PUSH(sy, 0, 0);
	    }	
	  break;
	case ieee_function_minus_enum:
	    {
	      bfd_vma value1, value2;
	      asection *section1, *section_dummy;
	      ieee_symbol_index_type sy;
	      next_byte(ieee);

	      POP(sy, section1, value1);
	      POP(sy, section_dummy, value2);
	      PUSH(sy, section1, value1-value2);
	    }
	  break;
	case ieee_function_plus_enum:
	    {
	      bfd_vma value1, value2;
	      asection *section1;
	      asection *section2;
	      ieee_symbol_index_type sy1;
	      ieee_symbol_index_type sy2;
	      next_byte(ieee);

	      POP(sy1, section1, value1);
	      POP(sy2, section2, value2);
	      PUSH(sy1.letter ? sy1 : sy2, section1 ? section1: section2, value1+value2);
	    }
	  break;
	default: 
	    {
	      bfd_vma va;
	      BFD_ASSERT(this_byte(ieee) < (int)ieee_variable_A_enum 
			 || this_byte(ieee) > (int)ieee_variable_Z_enum);
	      if (parse_int(ieee, &va)) 
		  {
		    PUSH(NOSYMBOL,0, va);
		  }
	      else {
		/* 
		  Thats all that we can understand. As far as I can see
		  there is a bug in the Microtec IEEE output which I'm
		  using to scan, whereby the comma operator is ommited
		  sometimes in an expression, giving expressions with too
		  many terms. We can tell if that's the case by ensuring
		  that sp == stack here. If not, then we've pushed
		  something too far, so we keep adding
		  */

		while (sp != stack+1) {
		  asection *section1;
		  ieee_symbol_index_type sy1;
		  POP(sy1, section1, *extra);
		}

		POP(*symbol, *section, *value);
		loop = false;
	      }
	    }

	}
  }
}



#define ieee_seek(abfd, offset) \
  ieee_data(abfd)->input_p = ieee_data(abfd)->first_byte + offset

static void
DEFUN(ieee_slurp_external_symbols,(abfd),
      bfd *abfd)
{
  ieee_data_type *ieee = ieee_data(abfd);
  file_ptr offset = ieee->w.r.external_part;

  ieee_symbol_type **prev_symbols_ptr = &ieee->external_symbols;
  ieee_symbol_type **prev_reference_ptr = &ieee->external_reference;
  ieee_symbol_type  *symbol;
  unsigned int symbol_count = 0;
  boolean loop = true;

  ieee->symbol_table_full = true;

  ieee_seek(abfd, offset );

  while (loop) {
    switch (this_byte(ieee)) {
    case ieee_external_symbol_enum:
      next_byte(ieee);
      symbol =  (ieee_symbol_type *)bfd_alloc(ieee->abfd, sizeof(ieee_symbol_type));

      *prev_symbols_ptr = symbol;
      prev_symbols_ptr= &symbol->next;
      symbol->index = must_parse_int(ieee);
      if (symbol->index > ieee->external_symbol_max_index) {
	ieee->external_symbol_max_index = symbol->index;
      }
      BFD_ASSERT (symbol->index >= ieee->external_symbol_min_index);
      symbol_count++;
      symbol->symbol.the_bfd = abfd;
      symbol->symbol.name = read_id(ieee);
      symbol->symbol.udata = (PTR)NULL;
      symbol->symbol.flags = BSF_NO_FLAGS;
      break;
    case ieee_attribute_record_enum >> 8:
      {
	unsigned int symbol_name_index;
	unsigned int symbol_type_index;
	unsigned int symbol_attribute_def;
	bfd_vma value;
	next_byte(ieee);	/* Skip prefix */
	next_byte(ieee);
	symbol_name_index = must_parse_int(ieee);
	symbol_type_index = must_parse_int(ieee);
	symbol_attribute_def = must_parse_int(ieee);

	parse_int(ieee,&value);

      }
      break;
    case ieee_value_record_enum >> 8:
      {
	unsigned int symbol_name_index;
	ieee_symbol_index_type symbol_ignore;
	boolean pcrel_ignore;
	unsigned int extra;
	next_byte(ieee);
	next_byte(ieee);

	symbol_name_index = must_parse_int(ieee);
	parse_expression(ieee,
	     &symbol->symbol.value,
	     &symbol->symbol.section,
	     &symbol_ignore, 
	     &pcrel_ignore, 
			 &extra);
	if (symbol->symbol.section != (asection *)NULL) {
	  symbol->symbol.flags  = BSF_GLOBAL | BSF_EXPORT;
	}
	else {
	  symbol->symbol.flags  = BSF_GLOBAL | BSF_EXPORT | BSF_ABSOLUTE;
	}
      }
      break;
    case ieee_weak_external_reference_enum:
      { bfd_vma size;
	bfd_vma value ;
	next_byte(ieee);
	/* Throw away the external reference index */
	(void)must_parse_int(ieee);
	/* Fetch the default size if not resolved */
	size = must_parse_int(ieee);
	/* Fetch the defautlt value if available */
	if (  parse_int(ieee, &value) == false) {
	  value = 0;
	}
	/* This turns into a common */
	symbol->symbol.flags = BSF_FORT_COMM;
	symbol->symbol.value = size;
      }
      break;

    case ieee_external_reference_enum: 
      next_byte(ieee);
      symbol = (ieee_symbol_type *)bfd_alloc(ieee->abfd, sizeof(ieee_symbol_type));
      symbol_count++;
      *prev_reference_ptr = symbol;
      prev_reference_ptr = &symbol->next;
      symbol->index = must_parse_int(ieee);
      symbol->symbol.the_bfd = abfd;
      symbol->symbol.name = read_id(ieee);
      symbol->symbol.udata = (PTR)NULL;
      symbol->symbol.section = (asection *)NULL;
      symbol->symbol.value = (bfd_vma)0;
      symbol->symbol.flags = BSF_UNDEFINED;
      if (symbol->index > ieee->external_reference_max_index) {
	ieee->external_reference_max_index = symbol->index;
      }
      BFD_ASSERT (symbol->index >= ieee->external_reference_min_index);
      break;

    default:
      loop = false;
    }
  }

  if (ieee->external_symbol_max_index != 0) {
    ieee->external_symbol_count = 
      ieee->external_symbol_max_index -
	ieee->external_symbol_min_index + 1  ;
  }
  else  {
    ieee->external_symbol_count = 0;
  }


  if(ieee->external_reference_max_index != 0) {
    ieee->external_reference_count = 
      ieee->external_reference_max_index -
	ieee->external_reference_min_index + 1;
  }
  else {
    ieee->external_reference_count = 0;
  }

  abfd->symcount =
    ieee->external_reference_count +  ieee->external_symbol_count;

  if (symbol_count != abfd->symcount) {
    /* There are gaps in the table -- */
    ieee->symbol_table_full = false;
  }
  *prev_symbols_ptr = (ieee_symbol_type *)NULL;
  *prev_reference_ptr = (ieee_symbol_type *)NULL;
}

static void
DEFUN(ieee_slurp_symbol_table,(abfd),
      bfd *abfd)
{
  if (ieee_data(abfd)->read_symbols == false) {
    ieee_slurp_external_symbols(abfd);
    ieee_data(abfd)->read_symbols= true;
  }
}

unsigned int
DEFUN(ieee_get_symtab_upper_bound,(abfd),
      bfd *abfd)
{
  ieee_slurp_symbol_table (abfd);

  return (abfd->symcount != 0) ? 
    (abfd->symcount+1) * (sizeof (ieee_symbol_type *)) : 0;
}

/* 
Move from our internal lists to the canon table, and insert in
symbol index order
*/

extern bfd_target ieee_vec;
unsigned int
DEFUN(ieee_get_symtab,(abfd, location),
      bfd *abfd AND
      asymbol **location)
{
  ieee_symbol_type *symp;
  static bfd dummy_bfd;
  static asymbol empty_symbol =
    { &dummy_bfd," ieee empty",(symvalue)0,BSF_DEBUGGING | BSF_ABSOLUTE};


  ieee_data_type *ieee = ieee_data(abfd);
  dummy_bfd.xvec= &ieee_vec;
  ieee_slurp_symbol_table(abfd);

  if (ieee->symbol_table_full == false) {
    /* Arrgh - there are gaps in the table, run through and fill them */
    /* up with pointers to a null place */
    unsigned int i;
    for (i= 0; i < abfd->symcount; i++) {
      location[i] = &empty_symbol;
    }
  }


  ieee->external_symbol_base_offset= -  ieee->external_symbol_min_index;
  for (symp = ieee_data(abfd)->external_symbols;
       symp != (ieee_symbol_type *)NULL;
       symp = symp->next) {
    /* Place into table at correct index locations */
    location[symp->index + ieee->external_symbol_base_offset] = &symp->symbol;

  }

  /* The external refs are indexed in a bit */
  ieee->external_reference_base_offset   =
    -  ieee->external_reference_min_index +ieee->external_symbol_count ;

  for (symp = ieee_data(abfd)->external_reference;
       symp != (ieee_symbol_type *)NULL;
       symp = symp->next) {
    location[symp->index + ieee->external_reference_base_offset] =
      &symp->symbol;

  }



  location[abfd->symcount] = (asymbol *)NULL;

  return abfd->symcount;
}


static void
DEFUN(ieee_slurp_sections,(abfd),
      bfd *abfd)
{
  ieee_data_type *ieee = ieee_data(abfd);
  file_ptr offset = ieee->w.r.section_part;

  asection *section = (asection *)NULL;

  if (offset != 0) {
    bfd_byte section_type[3];
    ieee_seek(abfd, offset);
    while (true) {
      switch (this_byte(ieee)) {
      case ieee_section_type_enum:
	{
	  unsigned int section_index ;
	  next_byte(ieee);
	  section_index = must_parse_int(ieee);
	  /* Fixme to be nice about a silly number of sections */
	  BFD_ASSERT(section_index < NSECTIONS);

	  section = bfd_make_section(abfd, " tempname");
	  ieee->section_table[section_index] = section;
	  section->flags = SEC_NO_FLAGS;
	  section->target_index = section_index;
	  section_type[0] =  this_byte_and_next(ieee);
	  switch (section_type[0]) {
	  case 0xC3:
	    section_type[1] = this_byte(ieee);
	    section->flags = SEC_LOAD;
	    switch (section_type[1]) {
	    case 0xD0:
	      /* Normal code */
	      next_byte(ieee);
	      section->flags |= SEC_LOAD | SEC_CODE;
	      break;
	    case 0xC4:
	      next_byte(ieee);
	      section->flags |= SEC_LOAD  | SEC_DATA;
	      /* Normal data */
	      break;
	    case 0xD2:
	      next_byte(ieee);
	      /* Normal rom data */
	      section->flags |= SEC_LOAD | SEC_ROM | SEC_DATA;
	      break;
	    default:
	      break;
	    }
	  }
	  section->name = read_id(ieee);
	  { bfd_vma parent, brother, context;
	    parse_int(ieee, &parent);
	    parse_int(ieee, &brother);
	    parse_int(ieee, &context);
	  }


	}
	break;
      case ieee_section_alignment_enum:
	{ 
	  unsigned int section_index;
	  bfd_vma value;
	  next_byte(ieee);
	  section_index = must_parse_int(ieee);
	  if (section_index > ieee->section_count) {
	    ieee->section_count = section_index;
	  }
	  ieee->section_table[section_index]->alignment_power =
	    bfd_log2(must_parse_int(ieee));
	  (void)parse_int(ieee, & value);
	}
	break;
      case ieee_e2_first_byte_enum: 
	{
	  ieee_record_enum_type t = read_2bytes(ieee);
	  switch (t) {
	  case ieee_section_size_enum:
	    section = ieee->section_table[must_parse_int(ieee)];
	    section->size = must_parse_int(ieee);
	    break;
	  case ieee_physical_region_size_enum:
	    section = ieee->section_table[must_parse_int(ieee)];
	    section->size = must_parse_int(ieee);
	    break;
	  case ieee_region_base_address_enum:
	    section = ieee->section_table[must_parse_int(ieee)];
	    section->vma = must_parse_int(ieee);
	    break;
	  case ieee_mau_size_enum:
	    must_parse_int(ieee);
	    must_parse_int(ieee);
	    break;
	  case ieee_m_value_enum:
	    must_parse_int(ieee);
	    must_parse_int(ieee);
	    break;
	  case ieee_section_base_address_enum:
	    section = ieee->section_table[must_parse_int(ieee)];
	    section->vma = must_parse_int(ieee);
	    break;
	  case ieee_section_offset_enum:
	    (void) must_parse_int(ieee);
	    (void) must_parse_int(ieee);
	    break;
	  default:
	    return;
	  }
	}
	break;
      default:
	return;
      }
    }
  }
}

/***********************************************************************
*  archive stuff 
*/
bfd_target *
DEFUN(ieee_archive_p,(abfd),
      bfd *abfd)
{
  return 0;			
#if 0
  char *library;
  boolean loop;
  ieee_ar_data_type *ar;
  unsigned int i;


/* FIXME */
  ieee_seek(abfd, (file_ptr) 0);
  if (this_byte(abfd) != Module_Beginning) return (bfd_target*)NULL;
  next_byte(ieee);
  library= read_id(ieee);
  if (strcmp(library , "LIBRARY") != 0) {
    free(library);
    return (bfd_target *)NULL;
  }
  /* Throw away the filename */
  free( read_id(ieee));
  /* This must be an IEEE archive, so we'll buy some space to do
     things */
  ar = (ieee_ar_data_type *) malloc(sizeof(ieee_ar_data_type));
  set_tdata (abfd, ar);
  ar->element_count = 0;
  ar->element_index = 0;
  obstack_init(&ar->element_obstack);

  next_byte(ieee);		/* Drop the ad part */
  must_parse_int(ieee);		/* And the two dummy numbers */
  must_parse_int(ieee);

  loop = true;
  /* Read the index of the BB table */
  while (loop) {
    ieee_ar_obstack_type t; 
    int rec =read_2bytes(abfd);
    if (rec ==ieee_assign_value_to_variable_enum) {
      int record_number = must_parse_int(ieee);
      t.file_offset = must_parse_int(ieee);
      t.abfd = (bfd *)NULL;
      ar->element_count++;
      obstack_grow(&ar->element_obstack, (PTR)&t, sizeof(t));
    }
    else loop = false;
  }
  ar->elements = (ieee_ar_obstack_type *)obstack_base(&ar->element_obstack);

  /* Now scan the area again, and replace BB offsets with file */
  /* offsets */


  for (i = 2; i < ar->element_count; i++) {
    ieee_seek(abfd, ar->elements[i].file_offset);
    next_byte(ieee);		/* Drop F8 */
    next_byte(ieee);		/* Drop 14 */
    must_parse_int(ieee);	/* Drop size of block */
    if (must_parse_int(ieee) != 0) {
      /* This object has been deleted */
      ar->elements[i].file_offset = 0;
    }
    else {
      ar->elements[i].file_offset = must_parse_int(ieee);
    }
  }

  obstack_finish(&ar->element_obstack);
  return abfd->xvec;
#endif
}

static boolean
DEFUN(ieee_mkobject,(abfd),
      bfd *abfd)
{
  ieee_data(abfd) = (ieee_data_type *)bfd_alloc(abfd,sizeof(ieee_data_type));
  return true;
}

bfd_target *
DEFUN(ieee_object_p,(abfd),
      bfd *abfd)
{
  char *processor;
  unsigned int part;
  ieee_data_type *ieee;
  uint8e_type buffer[300];
  ieee_data_type *save = ieee_data(abfd);
  set_tdata (abfd, 0);
  ieee_mkobject(abfd);
  ieee = ieee_data(abfd);
  
  /* Read the first few bytes in to see if it makes sense */
  bfd_read((PTR)buffer, 1, sizeof(buffer), abfd);

  ieee->input_p = buffer;
  if (this_byte_and_next(ieee) != Module_Beginning) goto fail;

  ieee->read_symbols= false;
  ieee->read_data= false;
  ieee->section_count = 0;
  ieee->external_symbol_max_index = 0;
  ieee->external_symbol_min_index = IEEE_PUBLIC_BASE;
  ieee->external_reference_min_index =IEEE_REFERENCE_BASE;
  ieee->external_reference_max_index = 0;
  ieee->abfd = abfd;
  memset((PTR)ieee->section_table, 0,	 sizeof(ieee->section_table));

  processor = ieee->mb.processor = read_id(ieee);
  if (strcmp(processor,"LIBRARY") == 0) goto fail;
  ieee->mb.module_name = read_id(ieee);
  if (abfd->filename == (char *)NULL) {
    abfd->filename =  ieee->mb.module_name;
  }
  /* Determine the architecture and machine type of the object file.  */
  bfd_scan_arch_mach(processor, &abfd->obj_arch, &abfd->obj_machine);

  if (this_byte(ieee) != ieee_address_descriptor_enum) {
    goto fail;
  }
  next_byte(ieee);	

  if (parse_int(ieee, &ieee->ad.number_of_bits_mau) == false) {
    goto fail;
  }
  if(parse_int(ieee, &ieee->ad.number_of_maus_in_address) == false) {
    goto fail;
  }

  /* If there is a byte order info, take it */
  if (this_byte(ieee) == ieee_variable_L_enum ||
      this_byte(ieee) == ieee_variable_M_enum)
    next_byte(ieee);


  for (part = 0; part < N_W_VARIABLES; part++) {
    boolean ok;
    if (read_2bytes(ieee) != ieee_assign_value_to_variable_enum) {
      goto fail;
    }
    if (this_byte_and_next(ieee) != part)  {
      goto fail;
    }

    ieee->w.offset[part] = parse_i(ieee, &ok);
    if (ok==false) {
      goto fail;
    }

  }
  abfd->flags = HAS_SYMS;

/* By now we know that this is a real IEEE file, we're going to read
   the whole thing into memory so that we can run up and down it
   quickly. We can work out how big the file is from the trailer
   record */

  ieee_data(abfd)->first_byte = (uint8e_type *) bfd_alloc(ieee->abfd, ieee->w.r.me_record
					    + 50);
  bfd_seek(abfd, 0, 0);
  bfd_read((PTR)(ieee_data(abfd)->first_byte), 1,   ieee->w.r.me_record+50,  abfd);

  ieee_slurp_sections(abfd);
  return abfd->xvec;
 fail:
  (void)  bfd_release(abfd, ieee);
  ieee_data(abfd) = save;
  return (bfd_target *)NULL;
}


void 
DEFUN(ieee_print_symbol,(ignore_abfd, afile,  symbol, how),
      bfd *ignore_abfd AND
      PTR afile AND
      asymbol *symbol AND
      bfd_print_symbol_enum_type how)
{
  FILE *file = (FILE *)afile;

  switch (how) {
  case bfd_print_symbol_name_enum:
    fprintf(file,"%s", symbol->name);
    break;
  case bfd_print_symbol_type_enum:
#if 0
    fprintf(file,"%4x %2x",aout_symbol(symbol)->desc & 0xffff,
	    aout_symbol(symbol)->other  & 0xff);
#endif
    BFD_FAIL();
    break;
  case bfd_print_symbol_all_enum:
      {
	CONST char *section_name = symbol->section == (asection *)NULL ?
	  "*abs" : symbol->section->name;
	if (symbol->name[0] == ' ') {
	  fprintf(file,"* empty table entry ");
	}
	else {
	  bfd_print_symbol_vandf((PTR)file,symbol);

	  fprintf(file," %-5s %04x %02x %s",
		  section_name,
		  (unsigned)	      ieee_symbol(symbol)->index,
		  (unsigned)	      0, /*
					   aout_symbol(symbol)->desc & 0xffff,
					   aout_symbol(symbol)->other  & 0xff,*/
		  symbol->name);
	}
      }
    break;
  }
}


static boolean
DEFUN(do_one,(ieee, current_map, location_ptr,s),
      ieee_data_type *ieee AND
      ieee_per_section_type *current_map AND
      uint8e_type *location_ptr AND
      asection *s)
{
  switch (this_byte(ieee))  
      {
      case ieee_load_constant_bytes_enum:
	  {
	    unsigned int number_of_maus;
	    unsigned int i;
	    next_byte(ieee);
	    number_of_maus = must_parse_int(ieee);

	    for (i = 0; i < number_of_maus; i++) {
	      location_ptr[current_map->pc++]= this_byte(ieee);
	      next_byte(ieee);
	    }
	  }
	break;

      case ieee_load_with_relocation_enum:
	  {
	    boolean loop = true;
	    next_byte(ieee);
	    while (loop) 
		{
		  switch (this_byte(ieee)) 
		      {
		      case ieee_variable_R_enum:

		      case ieee_function_signed_open_b_enum:
		      case ieee_function_unsigned_open_b_enum:
		      case ieee_function_either_open_b_enum:
			  {
			    unsigned int extra = 4;
			    boolean pcrel = false;

			    ieee_reloc_type *r = 
			      (ieee_reloc_type *) bfd_alloc(ieee->abfd,
							    sizeof(ieee_reloc_type));

			    *(current_map->reloc_tail_ptr) = r;
			    current_map->reloc_tail_ptr= &r->next;
			    r->next = (ieee_reloc_type *)NULL;
			    next_byte(ieee);
			    parse_expression(ieee,
					     &r->relent.addend,
					     &r->relent.section,
					     &r->symbol,
					     &pcrel, &extra);
			    r->relent.address = current_map->pc;
			    s->reloc_count++;
			    switch (this_byte(ieee)) {
			    case ieee_function_signed_close_b_enum:
			      next_byte(ieee);
			      break;
			    case ieee_function_unsigned_close_b_enum:
			      next_byte(ieee);
			      break;
			    case ieee_function_either_close_b_enum:
			      next_byte(ieee);
			      break;
			    default:
			      break;
			    }
			    /* Build a relocation entry for this type */
			    if (this_byte(ieee) == ieee_comma) {
			      next_byte(ieee);
			      /* Fetch number of bytes to pad */
			      extra = must_parse_int(ieee);
			    };
		   
			    /* If pc rel then stick -ve pc into instruction
			       and take out of reloc*/
		    
			    switch (extra) 
				{
				case 0:
				case 4:
				  if (pcrel == true) 
				      {
					bfd_putlong(ieee->abfd, -current_map->pc, location_ptr +
						    current_map->pc);
					r->relent.howto = &rel32_howto;
					r->relent.addend -= current_map->pc;
				      }
				  else 
				      {
					bfd_putlong(ieee->abfd, 0, location_ptr +
						    current_map->pc);
					r->relent.howto = &abs32_howto;
				      }
				  current_map->pc +=4;
				  break;
				case 2:
				  if (pcrel == true) {
				    bfd_putshort(ieee->abfd, (int)(-current_map->pc),  location_ptr +current_map->pc);
				    r->relent.addend -= current_map->pc;
				    r->relent.howto = &rel16_howto;
				  }
				  else {
				    bfd_putshort(ieee->abfd, 0,  location_ptr +current_map->pc);
				    r->relent.howto = &abs16_howto;
				  }
				  current_map->pc +=2;
				  break;

				default:
				  BFD_FAIL();
				  break;
				}
			  }
			break;
		      default: 
			  {
			    bfd_vma this_size ;
			    if (parse_int(ieee, &this_size) == true) {
			      unsigned int i;
			      for (i = 0; i < this_size; i++) {
				location_ptr[current_map->pc ++] = this_byte(ieee);
				next_byte(ieee);
			      }
			    }
			    else {
			      loop = false;
			    }
			  }
		      }
		}
	  }
      }
}

      /* Read in all the section data and relocation stuff too */
static boolean 
DEFUN(ieee_slurp_section_data,(abfd),
      bfd *abfd)
{
  bfd_byte *location_ptr ;
  ieee_data_type *ieee = ieee_data(abfd);
  unsigned int section_number ;

  ieee_per_section_type *current_map;
  asection *s;
  /* Seek to the start of the data area */
  if (ieee->read_data== true)  return true;
  ieee->read_data = true;
  ieee_seek(abfd, ieee->w.r.data_part);

  /* Allocate enough space for all the section contents */


  for (s = abfd->sections; s != (asection *)NULL; s = s->next) {
    ieee_per_section_type *per = (ieee_per_section_type *) s->used_by_bfd;
    per->data = (bfd_byte *) bfd_alloc(ieee->abfd, s->size);
    /*SUPPRESS 68*/
    per->reloc_tail_ptr =
      (ieee_reloc_type **)&(s->relocation);
  }



  while (true) {
    switch (this_byte(ieee)) 
	{
	  /* IF we see anything strange then quit */
	default:
	  return true;

	case ieee_set_current_section_enum:
	  next_byte(ieee);
	  section_number = must_parse_int(ieee);
	  s = ieee->section_table[section_number];
	  current_map = (ieee_per_section_type *) s->used_by_bfd;
	  location_ptr = current_map->data - s->vma;
	  /* The document I have says that Microtec's compilers reset */
	  /* this after a sec section, even though the standard says not */
	  /* to. SO .. */
	  current_map->pc =s->vma;
	  break;


	case ieee_e2_first_byte_enum:
	  next_byte(ieee);
	  switch (this_byte(ieee))
	      {
	      case ieee_set_current_pc_enum & 0xff:
		  {
		    bfd_vma value;
		    asection *dsection;
		    ieee_symbol_index_type symbol;
		    unsigned int extra;
		    boolean pcrel;
		    next_byte(ieee);
		    must_parse_int(ieee); /* Thow away section #*/
		    parse_expression(ieee, &value, &dsection, &symbol,
				     &pcrel, &extra);
		    current_map->pc = value;
		    BFD_ASSERT((unsigned)(value - s->vma) <= s->size);
		  }
		break;

	      case ieee_value_starting_address_enum & 0xff:
		/* We've got to the end of the data now - */
		return true;
	      default:
		BFD_FAIL();
		return true;
	      }
	  break;
	case ieee_repeat_data_enum:
	    {
	      /* Repeat the following LD or LR n times - we do this by
		 remembering the stream pointer before running it and
		 resetting it and running it n times 
		 */

	      unsigned int iterations ;
	      uint8e_type *start ;
	      next_byte(ieee);
	      iterations = must_parse_int(ieee);
	      start =  ieee->input_p;
	      while (iterations != 0) {
		ieee->input_p = start;
		do_one(ieee, current_map, location_ptr,s);
		iterations --;
	      }
	    }
	  break;
	case ieee_load_constant_bytes_enum:
	case ieee_load_with_relocation_enum:
	    {
	      do_one(ieee, current_map, location_ptr,s);
	    }
	}
  }
}





boolean
DEFUN(ieee_new_section_hook,(abfd, newsect),
      bfd *abfd AND
      asection *newsect)
{
  newsect->used_by_bfd = (PTR)
    bfd_alloc(abfd, sizeof(ieee_per_section_type));
  ieee_per_section( newsect)->data = (bfd_byte *)NULL;
  ieee_per_section(newsect)->section = newsect;
  return true;
}


unsigned int
DEFUN(ieee_get_reloc_upper_bound,(abfd, asect),
      bfd *abfd AND
      sec_ptr asect)
{
  ieee_slurp_section_data(abfd);
  return (asect->reloc_count+1) * sizeof(arelent *);
}

static boolean
DEFUN(ieee_get_section_contents,(abfd, section, location, offset, count),
      bfd *abfd AND
      sec_ptr section AND
      PTR location AND
      file_ptr offset AND
      int count)
{
  ieee_per_section_type *p = (ieee_per_section_type *) section->used_by_bfd;
  ieee_slurp_section_data(abfd);
  (void)  memcpy(location, p->data + offset, count);
  return true;
}


unsigned int
DEFUN(ieee_canonicalize_reloc,(abfd, section, relptr, symbols),
      bfd *abfd AND
      sec_ptr section AND
      arelent **relptr AND
      asymbol **symbols)
{
/*  ieee_per_section_type *p = (ieee_per_section_type *) section->used_by_bfd;*/
  ieee_reloc_type *src = (ieee_reloc_type *)(section->relocation);
  ieee_data_type *ieee = ieee_data(abfd);

  while (src != (ieee_reloc_type *)NULL) {
    /* Work out which symbol to attatch it this reloc to */
    switch (src->symbol.letter) {
    case 'X':
      src->relent.sym_ptr_ptr =
	symbols + src->symbol.index +  ieee->external_reference_base_offset;
      break;
    case 0:
      src->relent.sym_ptr_ptr = (asymbol **)NULL;
      break;
    default:

      BFD_FAIL();
    }
    *relptr++ = &src->relent;
    src = src->next;
  }
  *relptr = (arelent *)NULL;
  return section->reloc_count;
}

boolean
DEFUN(ieee_set_arch_mach,(abfd, arch, machine),
      bfd *abfd AND
      enum bfd_architecture arch AND
      unsigned long machine)
{
  abfd->obj_arch = arch;
  abfd->obj_machine = machine;
  return true;
}


static int 
DEFUN(comp,(ap, bp),
     CONST PTR ap AND
     CONST PTR bp)
{
  arelent *a = *((arelent **)ap);
  arelent *b = *((arelent **)bp);
  return a->address - b->address;
}

/*
Write the section headers
*/

static void
DEFUN(ieee_write_section_part,(abfd),
      bfd *abfd)
{
  ieee_data_type *ieee = ieee_data(abfd);
  asection *s;
  ieee->w.r.section_part = bfd_tell(abfd);
  for (s = abfd->sections; s != (asection *)NULL; s=s->next) {
    ieee_write_byte(abfd, ieee_section_type_enum);
    ieee_write_byte(abfd, s->index + IEEE_SECTION_NUMBER_BASE);

    switch (s->flags & (SEC_LOAD | SEC_CODE | SEC_DATA | SEC_ROM)) {
    case  SEC_LOAD | SEC_CODE:
      /* Normal named section, code */
      ieee_write_byte(abfd, ieee_variable_C_enum);
      ieee_write_byte(abfd, ieee_variable_P_enum);
      break;
    case  SEC_LOAD | SEC_DATA:
      /* Normal named section, data */
      ieee_write_byte(abfd, ieee_variable_C_enum);
      ieee_write_byte(abfd, ieee_variable_D_enum);
      break;
    case  SEC_LOAD | SEC_DATA | SEC_ROM:
      /* Normal named section, data rom */
      ieee_write_byte(abfd, ieee_variable_C_enum);
      ieee_write_byte(abfd, ieee_variable_R_enum);
      break;
    default:
      ieee_write_byte(abfd, ieee_variable_C_enum);
      break;
    }

    ieee_write_id(abfd, s->name);
    ieee_write_int(abfd, 0);	/* Parent */
    ieee_write_int(abfd, 0);	/* Brother */
    ieee_write_int(abfd, 0);	/* Context */

    /* Alignment */
    ieee_write_byte(abfd, ieee_section_alignment_enum);
    ieee_write_byte(abfd, s->index + IEEE_SECTION_NUMBER_BASE);
    ieee_write_int(abfd, 1 << s->alignment_power);

    /* Size */
    ieee_write_2bytes(abfd, ieee_section_size_enum);
    ieee_write_byte(abfd, s->index + IEEE_SECTION_NUMBER_BASE);
    ieee_write_int(abfd, s->size);

    /* Vma */
    ieee_write_2bytes(abfd, ieee_region_base_address_enum);
    ieee_write_byte(abfd, s->index + IEEE_SECTION_NUMBER_BASE);
    ieee_write_int(abfd, s->vma);

  }
}



/* write the data in an ieee way */
static void
DEFUN(ieee_write_data_part,(abfd),
      bfd *abfd)
{
  asection *s;
  ieee_data_type *ieee = ieee_data(abfd);
  ieee->w.r.data_part = bfd_tell(abfd);
  for (s = abfd->sections; s != (asection *)NULL; s = s->next) 
      {
	bfd_byte  header[11];
	bfd_byte *stream = ieee_per_section(s)->data;
	arelent **p = s->orelocation;
	unsigned int relocs_to_go = s->reloc_count;
	size_t current_byte_index = 0;

	/* Sort the reloc records so we can insert them in the correct
	   places */
	if (s->reloc_count != 0) {
	  qsort(s->orelocation,
		relocs_to_go,
		sizeof(arelent **),
		comp);
	}


	/* Output the section preheader */
	header[0] =ieee_set_current_section_enum;
	header[1] = s->index + IEEE_SECTION_NUMBER_BASE;

	header[2] = ieee_set_current_pc_enum >> 8;
	header[3]= ieee_set_current_pc_enum  & 0xff;
	header[4] = s->index + IEEE_SECTION_NUMBER_BASE;
	ieee_write_int5(header+5, s->vma );
	header[10] = ieee_load_with_relocation_enum;
	bfd_write((PTR)header, 1, sizeof(header), abfd);

	/* Output the data stream as the longest sequence of bytes
	   possible, allowing for the a reasonable packet size and
	   relocation stuffs */

	if ((PTR)stream == (PTR)NULL) {
	  /* Outputting a section without data, fill it up */
	  stream = (uint8e_type *)(bfd_alloc(abfd, s->size));
	  memset((PTR)stream, 0, s->size);
	}
	while (current_byte_index < s->size) {
	  size_t run;
	  unsigned int MAXRUN = 32;
	  if (relocs_to_go) {
	    run = (*p)->address - current_byte_index;
	  }
	  else {
	    run = MAXRUN;
	  }
	  if (run > s->size - current_byte_index) {
	    run = s->size - current_byte_index;
	  }

	  if (run != 0) {
	    /* Output a stream of bytes */
	    ieee_write_int(abfd, run);
	    bfd_write((PTR)(stream + current_byte_index), 
		      1,
		      run,
		      abfd);
	    current_byte_index += run;
	  }
	  /* Output any relocations here */
	  if (relocs_to_go && (*p) && (*p)->address == current_byte_index) {
	    while (relocs_to_go && (*p) && (*p)->address == current_byte_index) {

	      arelent *r = *p;
	      bfd_vma ov;
	      if (r->howto->pc_relative) {
		r->addend += current_byte_index;
	      }

	      switch (r->howto->size) {
	      case 2:

		ov = bfd_getlong(abfd,
				 stream+current_byte_index);
	    current_byte_index +=4;
		break;
	      case 1:
		ov = bfd_getshort(abfd,
				  stream+current_byte_index);
	    current_byte_index +=2;
		break;
	      default:
		BFD_FAIL();
	      }
	      ieee_write_byte(abfd, ieee_function_either_open_b_enum);

	      if (r->sym_ptr_ptr != (asymbol **)NULL) {
		ieee_write_expression(abfd, r->addend + ov,
				      r->section,
				      *(r->sym_ptr_ptr),
				      r->howto->pc_relative, s->target_index);
	      }
	      else {
		ieee_write_expression(abfd, r->addend + ov,
				      r->section,
				      (asymbol *)NULL,
				      r->howto->pc_relative, s->target_index);
	      }
	      ieee_write_byte(abfd,
			      ieee_function_either_close_b_enum);
	      if (r->howto->size != 2) {
	      ieee_write_byte(abfd, ieee_comma);
	      ieee_write_int(abfd, 1<< r->howto->size);
	    }

	      relocs_to_go --;
	      p++;
	    }

	  }
	}
      }

}



static void
DEFUN(init_for_output,(abfd),
      bfd *abfd)
{
  asection *s; 
  for (s = abfd->sections; s != (asection *)NULL; s = s->next) {
    if (s->size != 0) {
      ieee_per_section(s)->data = (bfd_byte *)(bfd_alloc(abfd, s->size));
    }
  }
}

/** exec and core file sections */

/* set section contents is complicated with IEEE since the format is 
* not a byte image, but a record stream.
*/
boolean
DEFUN(ieee_set_section_contents,(abfd, section, location, offset, count),
      bfd *abfd AND
      sec_ptr section AND
      PTR location AND
      file_ptr offset AND
      int count)
{
  if (ieee_per_section(section)->data == (bfd_byte *)NULL) {
    init_for_output(abfd);
  }
  (void) memcpy(ieee_per_section(section)->data + offset, location, count);
  return true;
}

/*
write the external symbols of a file, IEEE considers two sorts of
external symbols, public, and referenced. It uses to internal forms
to index them as well. When we write them out we turn their symbol
values into indexes from the right base.
*/
static void
DEFUN(ieee_write_external_part,(abfd),
      bfd *abfd)
{
  asymbol **q;
  ieee_data_type *ieee = ieee_data(abfd);

  unsigned int reference_index = IEEE_REFERENCE_BASE;
  unsigned int public_index = IEEE_PUBLIC_BASE;
  ieee->w.r.external_part = bfd_tell(abfd);
  if (abfd->outsymbols != (asymbol **)NULL) {
    for (q = abfd->outsymbols; *q  != (asymbol *)NULL; q++) {
      asymbol *p = *q;
      if (p->flags & BSF_UNDEFINED) {
	/* This must be a symbol reference .. */
	ieee_write_byte(abfd, ieee_external_reference_enum);
	ieee_write_int(abfd, reference_index);
	ieee_write_id(abfd, p->name);
	p->value = reference_index;
	reference_index++;
      }
      else if(p->flags & BSF_FORT_COMM) {
	/* This is a weak reference */
	ieee_write_byte(abfd, ieee_external_reference_enum);
	ieee_write_int(abfd, reference_index);
	ieee_write_id(abfd, p->name);
	ieee_write_byte(abfd, ieee_weak_external_reference_enum);
	ieee_write_int(abfd, reference_index);
	ieee_write_int(abfd, p->value);
	ieee_write_int(abfd, BFD_FORT_COMM_DEFAULT_VALUE);
	p->value = reference_index;
	reference_index++;
      }
      else if(p->flags & BSF_GLOBAL) {
	/* This must be a symbol definition */

	ieee_write_byte(abfd, ieee_external_symbol_enum);
	ieee_write_int(abfd, public_index );
	ieee_write_id(abfd, p->name);

	/* Write out the value */
	ieee_write_2bytes(abfd, ieee_value_record_enum);
	ieee_write_int(abfd, public_index);
	if (p->section != (asection *)NULL)
	  {
	    ieee_write_expression(abfd,
				  p->value + p->section->output_offset,
				  p->section->output_section,
				  (asymbol *)NULL, false, 0);
	  }
	else
	  {
	    ieee_write_expression(abfd,
				  p->value,
				  (asection *)NULL,
				  (asymbol *)NULL, false, 0);
	  }
	p->value = public_index;
	public_index++;
      }
      else {
	/* This can happen - when there are gaps in the symbols read */
	/* from an input ieee file */
      }
    }
  }

}

static
void 
DEFUN(ieee_write_me_part,(abfd),
      bfd *abfd)
{
  ieee_data_type *ieee= ieee_data(abfd);
  ieee->w.r.me_record = bfd_tell(abfd);

  ieee_write_2bytes(abfd, ieee_value_starting_address_enum);
  ieee_write_int(abfd, abfd->start_address);
  ieee_write_byte(abfd, ieee_module_end_enum);

}
boolean
DEFUN(ieee_write_object_contents,(abfd),
      bfd *abfd)
{
  ieee_data_type *ieee = ieee_data(abfd);
  unsigned int i;
  file_ptr   old;
  /* Fast forward over the header area */
  bfd_seek(abfd, 0, 0);
  ieee_write_byte(abfd, ieee_module_beginning_enum);

  ieee_write_id(abfd, bfd_printable_arch_mach(abfd->obj_arch,
					      abfd->obj_machine));
  ieee_write_id(abfd, abfd->filename);
  ieee_write_byte(abfd, ieee_address_descriptor_enum);
  ieee_write_byte(abfd, 8);	/* Bits per MAU */
  ieee_write_byte(abfd, 4);	/* MAU's per address */

  /* Fast forward over the variable bits */	
  old = bfd_tell(abfd);
  bfd_seek(abfd, 8 * N_W_VARIABLES, 1);

  /*
    First write the symbols, this changes their values into table 
    indeces so we cant use it after this point
    */
  ieee_write_external_part(abfd);     
  ieee_write_byte(abfd, ieee_record_seperator_enum);

  ieee_write_section_part(abfd);
  ieee_write_byte(abfd, ieee_record_seperator_enum);
  /* 
    Can only write the data once the symbols have been written since
    the data contains relocation information which points to the
    symbols 
    */
  ieee_write_data_part(abfd);     
  ieee_write_byte(abfd, ieee_record_seperator_enum);

  /*
    At the end we put the end !
    */
  ieee_write_me_part(abfd);


  /* Generate the header */
  bfd_seek(abfd, old, false);

  for (i= 0; i < N_W_VARIABLES; i++) {
    ieee_write_2bytes(abfd,ieee_assign_value_to_variable_enum);
    ieee_write_byte(abfd, i);
    ieee_write_int5_out(abfd, ieee->w.offset[i]);
  }
  return true;
}




/* Native-level interface to symbols. */

/* We read the symbols into a buffer, which is discarded when this
function exits.  We read the strings into a buffer large enough to
hold them all plus all the cached symbol entries. */

asymbol *
DEFUN(ieee_make_empty_symbol,(abfd),
      bfd *abfd)
{

  ieee_symbol_type  *new =
    (ieee_symbol_type *)zalloc (sizeof (ieee_symbol_type));
  new->symbol.the_bfd = abfd;
  return &new->symbol;

}

static bfd *
ieee_openr_next_archived_file(arch, prev)
bfd *arch;
bfd *prev;
{
  ieee_ar_data_type *ar = ieee_ar_data(arch);
  /* take the next one from the arch state, or reset */
  if (prev == (bfd *)NULL) {
    /* Reset the index - the first two entries are bogus*/
    ar->element_index = 2;
  }
  while (true) {  
    ieee_ar_obstack_type *p = ar->elements + ar->element_index;
    ar->element_index++;
    if (ar->element_index <= ar->element_count) {
      if (p->file_offset != (file_ptr)0) {
	if (p->abfd == (bfd *)NULL) {
	  p->abfd = _bfd_create_empty_archive_element_shell(arch);
	  p->abfd->origin = p->file_offset;
	}
	return p->abfd;
      }
    }
    else {
      return (bfd *)NULL;
    }

  }
}

static boolean
ieee_find_nearest_line(abfd,
			 section,
			 symbols,
			 offset,
			 filename_ptr,
			 functionname_ptr,
			 line_ptr)
bfd *abfd;
asection *section;
asymbol **symbols;
bfd_vma offset;
char **filename_ptr;
char **functionname_ptr;
int *line_ptr;
{
  return false;
}


static int
ieee_generic_stat_arch_elt(abfd, buf)
bfd *abfd;
struct stat *buf;
{
  ieee_ar_data_type *ar = ieee_ar_data(abfd);
  if (ar == (ieee_ar_data_type *)NULL) {
    bfd_error = invalid_operation;
    return -1;
  }
  else {
    buf->st_size = 0x1;
    buf->st_mode = 0666;
  return 0;
  }
}
static int 
DEFUN(ieee_sizeof_headers,(abfd, x),
      bfd *abfd AND
      boolean x)
{
  return 0;
}

#define ieee_core_file_failing_command (char *(*)())(bfd_nullvoidptr)
#define ieee_core_file_failing_signal (int (*)())bfd_0
#define ieee_core_file_matches_executable_p ( PROTO(boolean, (*),(bfd *, bfd *)))bfd_false
#define ieee_slurp_armap bfd_true
#define ieee_slurp_extended_name_table bfd_true
#define ieee_truncate_arname (void (*)())bfd_nullvoidptr
#define ieee_write_armap  (PROTO( boolean, (*),(bfd *, unsigned int, struct orl *, int, int))) bfd_nullvoidptr
#define ieee_get_lineno (struct lineno_cache_entry *(*)())bfd_nullvoidptr
#define	ieee_close_and_cleanup		bfd_generic_close_and_cleanup


/*SUPPRESS 460 */
bfd_target ieee_vec =
{
  "ieee",			/* name */
  bfd_target_ieee_flavour_enum,
  true,				/* target byte order */
  true,				/* target headers byte order */
  (HAS_RELOC | EXEC_P |		/* object flags */
   HAS_LINENO | HAS_DEBUG |
   HAS_SYMS | HAS_LOCALS | DYNAMIC | WP_TEXT | D_PAGED),
  (SEC_CODE|SEC_DATA|SEC_ROM|SEC_HAS_CONTENTS
   |SEC_ALLOC | SEC_LOAD | SEC_RELOC), /* section flags */
  ' ',				/* ar_pad_char */
  16,				/* ar_max_namelen */

  _do_getblong, _do_putblong, _do_getbshort, _do_putbshort, /* data */
  _do_getblong, _do_putblong, _do_getbshort, _do_putbshort, /* hdrs */

  { _bfd_dummy_target,
     ieee_object_p,		/* bfd_check_format */
     ieee_archive_p,
    _bfd_dummy_target,
     },
  {
    bfd_false,
    ieee_mkobject, 
    _bfd_generic_mkarchive,
    bfd_false
    },
  {
    bfd_false,
    ieee_write_object_contents,
    _bfd_write_archive_contents,
    bfd_false,
  },
  JUMP_TABLE(ieee)
};
