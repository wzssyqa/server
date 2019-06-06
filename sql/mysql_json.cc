#include "mysql_json.h"
#include "mysqld.h"             // key_memory_JSON
#include "sql_class.h"          // THD


static bool check_json_depth(size_t depth);
/**
  Read an offset or size field from a buffer. The offset could be either
  a two byte unsigned integer or a four byte unsigned integer.

  @param data  the buffer to read from
  @param large tells if the large or small storage format is used; true
               means read four bytes, false means read two bytes
*/

size_t read_offset_or_size(const char *data, bool large)
{
  return large ? uint4korr(data) : uint2korr(data);
}

/**
  Check if the depth of a JSON document exceeds the maximum supported
  depth (JSON_DOCUMENT_MAX_DEPTH). Raise an error if the maximum depth
  has been exceeded.

  @param[in] depth  the current depth of the document
  @return true if the maximum depth is exceeded, false otherwise
*/
static bool check_json_depth(size_t depth)
{
  if (depth > JSON_DOCUMENT_MAX_DEPTH)
  {
    // @todo anel implement errors
    //my_error(ER_JSON_DOCUMENT_TOO_DEEP, MYF(0));
    return true;
  }
  return false;
}

bool parse_value(String *buffer, size_t type, const char *data, size_t len, bool large, size_t depth)
{
  switch (type)
  {
  case JSONB_TYPE_SMALL_OBJECT:
  {
    return parse_array_or_object(buffer, Field_mysql_json::enum_type::OBJECT, data, len, false);
  }
  case JSONB_TYPE_LARGE_OBJECT:
    return parse_array_or_object(buffer, Field_mysql_json::enum_type::OBJECT, data, len, true);
  case JSONB_TYPE_SMALL_ARRAY:
    return parse_array_or_object(buffer, Field_mysql_json::enum_type::ARRAY, data, len, false);
  case JSONB_TYPE_LARGE_ARRAY:
    return parse_array_or_object(buffer, Field_mysql_json::enum_type::ARRAY, data, len, true);
  default:
    return parse_mysql_scalar(buffer, type, data, len, large, depth); // ovo ne radi
  }
}

bool parse_array_or_object(String *buffer,Field_mysql_json::enum_type t,
                           const char *data, size_t len, bool large)
{
  DBUG_ASSERT((t == Field_mysql_json::enum_type::ARRAY) ||
              (t == Field_mysql_json::enum_type::OBJECT));
  /*
    Make sure the document is long enough to contain the two length fields
    (both number of elements or members, and number of bytes).
  */
  const size_t offset_size= large ? LARGE_OFFSET_SIZE : SMALL_OFFSET_SIZE;
  if (len < 2 * offset_size)
    return true;

  // Calculate number of elements and length of binary (number of bytes)
  size_t element_count, bytes;
  
  element_count= read_offset_or_size(data, large);
  bytes= read_offset_or_size(data + offset_size, large);

  // The value can't have more bytes than what's available in the data buffer.
  if (bytes > len)
    return true;

  /*
    Calculate the size of the header. It consists of:
    - two length fields
    - if it is a JSON object, key entries with pointers to where the keys
      are stored
    - value entries with pointers to where the actual values are stored
  */

  //size_t header_size= 2 * offset_size;
  size_t key_json_offset, key_json_start, key_json_len;
  size_t type, value_type_offset, value_counter(0);
  char *key_element;
  bool is_last(false);

  if (element_count == 0)
  {
    if (t==Field_mysql_json::enum_type::OBJECT)
    {
      if(buffer->append("{}"))
      {
        return true;
      }
    }
    else
    {
      if(buffer->append("[]"))
      {
        return true;
      }
    }
    return false;
  }

  for(uint8 i=0; i<element_count; i++)
  {
    if (t==Field_mysql_json::enum_type::OBJECT)
    {
      // header_size+= element_count *
      // (large ? KEY_ENTRY_SIZE_LARGE : KEY_ENTRY_SIZE_SMALL);
      if(i==0)
      {
        if(buffer->append('{'))
        {
          return true;
        }
      }

      key_json_offset= 2*offset_size+i*(large?KEY_ENTRY_SIZE_LARGE:KEY_ENTRY_SIZE_SMALL);
      key_json_start= read_offset_or_size(data+key_json_offset,large);
      key_json_len= read_offset_or_size(data+key_json_offset+offset_size, large);
      
      key_element= new char[key_json_len+1];
      memmove(key_element, const_cast<char*>(&data[key_json_start]), key_json_len);
      key_element[key_json_len]= '\0';

      if(buffer->append('"'))
      {
        return true;
      }
      if( buffer->append(String((const char *)key_element, &my_charset_bin)) )
      {
        return true;
      }
      if(buffer->append('"'))
      {
        return true;
      }
      delete[] key_element;
      if(buffer->append(":"))
      {
        return true;
      }

      value_type_offset= 2*offset_size+ 
      (large?KEY_ENTRY_SIZE_LARGE:KEY_ENTRY_SIZE_SMALL)*(element_count)+
      (large ? VALUE_ENTRY_SIZE_LARGE : VALUE_ENTRY_SIZE_SMALL)*value_counter;
      value_counter++;

      if(i==(element_count-1))
      {
        is_last=true;
      }

      type= data[value_type_offset];
      //parse_value(buffer, type, data, len); // should be called which is 
      // calling  parse_mysql_scalar(buffer, type, data, len, large, 0)

      // Inlined values
      if (type == JSONB_TYPE_INT16 || type == JSONB_TYPE_UINT16 ||
      type == JSONB_TYPE_LITERAL ||
      (large && (type == JSONB_TYPE_INT32 || type == JSONB_TYPE_UINT32)))
      {
        if(parse_mysql_scalar(buffer, type, data + value_type_offset+1, len, large, 0))
        {
          return true;
        }
      }
      else // Non-inlined values
      {
        size_t val_len_ptr=read_offset_or_size(data+value_type_offset+1, large);
        //if(parse_mysql_scalar(buffer, type, data+val_len_ptr, len, large, 0))
        if(parse_value(buffer, type, data+val_len_ptr, bytes-val_len_ptr, large, 0))
        {
          return true;
        }

      }
      if(!is_last)
      {
        buffer->append(",");
      }

      if(i==(element_count-1))
      {
        if(buffer->append('}'))
        {
          return true;
        }
      }

    } // end object

    else // t==Field_mysql::enum_type::Array
    {
      if(i==0)
      {
        if(buffer->append('['))
        {
          return true;
        }
      }

      // Parse array
      value_type_offset= 2*offset_size+
      (large ? VALUE_ENTRY_SIZE_LARGE : VALUE_ENTRY_SIZE_SMALL)*value_counter;
      value_counter++;

      if(i==(element_count-1))
      {
        is_last=true;
      }

      type= data[value_type_offset];
      //parse_value(buffer, type, data, len); // should be called which is 
      // calling  parse_mysql_scalar(buffer, type, data, len, large, 0)

      // Inlined values
      if (type == JSONB_TYPE_INT16 || type == JSONB_TYPE_UINT16 ||
      type == JSONB_TYPE_LITERAL ||
      (large && (type == JSONB_TYPE_INT32 || type == JSONB_TYPE_UINT32)))
      {
        if(parse_mysql_scalar(buffer, type, data + value_type_offset+1, len, large, 0))
        {
          return true;
        }
      }
      else // Non-inlined values
      {
        size_t val_len_ptr=read_offset_or_size(data+value_type_offset+1, large);
        //if(parse_mysql_scalar(buffer, type, data+val_len_ptr, len, large, 0))
        if(parse_value(buffer, type, data+val_len_ptr, bytes-val_len_ptr, large, 0))
        {
          return true;
        }

      }

      if(!is_last)
      {
        buffer->append(",");
      }

      if(i==(element_count-1))
      {
        if(buffer->append(']'))
        {
          return true;
        }
      }
    } // end array
    is_last=false;

  } // end for
  
  return false;
}

// static bool read_variable_length(const char *data, size_t data_length,
//                                  size_t *length, size_t *num)
// {
//   /*
//     It takes five bytes to represent UINT_MAX32, which is the largest
//     supported length, so don't look any further.
//   */
//   const size_t max_bytes= std::min(data_length, static_cast<size_t>(5));

//   size_t len= 0;
//   for (size_t i= 0; i < max_bytes; i++)
//   {
//     // Get the next 7 bits of the length.
//     len|= (data[i] & 0x7f) << (7 * i);
//     if ((data[i] & 0x80) == 0)
//     {
//       // The length shouldn't exceed 32 bits.
//       if (len > UINT_MAX32)
//         return true;                          /* purecov: inspected */

//       // This was the last byte. Return successfully.
//       *num= i + 1;
//       *length= len;
//       return false;
//     }
//   }

//   // No more available bytes. Return true to signal error.
//   return true;                                /* purecov: inspected */
// }

bool parse_mysql_scalar(String* buffer, size_t value_json_type,
                        const char *data, size_t len, bool large, size_t depth)
{
  if (check_json_depth(++depth))
  {
    return true;
  }


  switch(value_json_type)
  {
    /** FINISHED WORKS **/
    case JSONB_TYPE_LITERAL:
    {
      switch (static_cast<uint8>(*data))
      {
        case JSONB_NULL_LITERAL:
        {
          if(buffer->append("null"))
          {
            return true; 
          }
          break;
        }
        case JSONB_TRUE_LITERAL:
        {
          if(buffer->append("true"))
          {
            return true; 
          }
          break;
        }
        case JSONB_FALSE_LITERAL:
        {
          if(buffer->append("false"))
          {
            return true; 
          }
          break;
        }
        default:
        {
          return true;
        }
        
      }
      break;
    }
    /** FINISHED WORKS **/
    case JSONB_TYPE_INT16 :
    {
      if(buffer->append_longlong((longlong) (sint2korr(data))))
      {
        return true;
      }
      break;
    }

    /** FINISHED WORKS **/
    case JSONB_TYPE_INT32:
    {
      char *value_element;
      uint num_bytes=MAX_BIGINT_WIDTH;
      value_element= new char[num_bytes+1];
      memmove(value_element, const_cast<char*>(&data[0]), num_bytes);
      value_element[num_bytes+1]= '\0';
      if( buffer->append_longlong(sint4korr(value_element)))
      {
        return true;
      }
      delete[] value_element;
      break;
    }

    /* FINISHED WORKS  */
    case JSONB_TYPE_INT64:
    {
      char *value_element;
      uint num_bytes=MAX_BIGINT_WIDTH;
      value_element= new char[num_bytes+1];
      memmove(value_element, const_cast<char*>(&data[0]), num_bytes);
      value_element[num_bytes+1]= '\0';
      if( buffer->append_longlong(sint8korr(value_element)))
      {
        return true;
      }
      delete[] value_element;
      break;
    }
    /** FINISHED WORKS **/
    case JSONB_TYPE_UINT16 : 
    {
      if(buffer->append_longlong((longlong) (uint2korr(data))))
      {
        return true;
      }
      break;
    }

    /** FINISHED WORKS **/
    case JSONB_TYPE_UINT32 : 
    {
      if(buffer->append_longlong((longlong) (uint4korr(data))))
      {
        return true;
      }
      break;
    }

    /** FINISHED WORKS **/
    case JSONB_TYPE_UINT64:
    {
      char *value_element;
      uint num_bytes=MAX_BIGINT_WIDTH;

      value_element= new char[num_bytes+1];
      memmove(value_element, const_cast<char*>(&data[0]),num_bytes);
      value_element[num_bytes+1]= '\0';
      if( buffer->append_ulonglong(uint8korr(value_element)))
      {
        return true;
      }
      delete[] value_element;
      break;
    }

    case JSONB_TYPE_DOUBLE:
    {
     //char *end;
      //double d=strtod(data, &end);
      // double d;
      // int error;
      // d=my_strtod(data, &end, &error);
      double d;
      float8get(d, data);
      buffer->qs_append(&d);
      break;
    }

    /** FINISHED WORKS **/
    case JSONB_TYPE_STRING:
    {
      size_t value_length;
      char *value_element;
      value_length= (uint) data[0];
      value_element= new char[value_length+1];
      memmove(value_element, const_cast<char*>(&data[1]),
              value_length);
      value_element[value_length]= '\0';
      if(buffer->append('"'))
      {
        return true;
      }
      if( buffer->append(String((const char *)value_element, &my_charset_bin)))
      {
        return true;
      }
      if(buffer->append('"'))
      {
        return true;
      }
      delete[] value_element;
      break;
    }

    /** testing **/
    case JSONB_TYPE_OPAQUE:
    {
      // The type is encoded as a uint8 that maps to an enum_field_types @todo anel
      uint8 type_byte= static_cast<uint8>(*data);
      Field_mysql_json::enum_json_type field_type= 
      static_cast<Field_mysql_json::enum_json_type>(type_byte);

      char *value_element;
      // For now we are assuming one byte length
      size_t length; 
      length= data[1]; // should be calculated depending on 8th bit of ith byte @todo
      value_element= new char[length+1];
      memmove(value_element, const_cast<char*>(&data[2]),
              len-2);
      switch(field_type)
      {
        case Field_mysql_json::enum_json_type::J_TIME: //11 mysql
        {
          MYSQL_TIME t;
          TIME_from_longlong_time_packed(&t, sint8korr(value_element));
          delete[] value_element;
          char *ptr= const_cast<char *>(buffer->ptr())+buffer->length();
          const int size= my_TIME_to_str(&t, ptr, 6);
          buffer->length(buffer->length() + size);
          break;
        }
        case Field_mysql_json::enum_json_type::J_DATE: //10 mysql
        //TIME_from_longlong_date_packed(ltime, packed_value); //not defined in sql/compat56.h
        case Field_mysql_json::enum_json_type::J_TIMESTAMP: //7 mysql
        case Field_mysql_json::enum_json_type::J_DATETIME: //12 mysql
        {
          MYSQL_TIME t;
          TIME_from_longlong_datetime_packed(&t, sint8korr(value_element));
          delete[] value_element;
          char *ptr= const_cast<char *>(buffer->ptr())+buffer->length();
          const int size= my_TIME_to_str(&t, ptr, 6);
          buffer->length(buffer->length() + size);
          break;
        }
        default:
          break;
      }
      // value_element[len-1]= '\0';
      // if( buffer->append(String((const char *)value_element+4, &my_charset_bin)))
      // {
      //   return true;
      // }
      // // if(parse_value(buffer, type_byte, data+val_len_ptr+1, len, large, 0))
      // // {
      // //   return true;
      // // }

      // Session 2
      // size_t str_len;
      // size_t n;
      // if (read_variable_length(data, len, &str_len, &n))
      //   return true;
      // parse_value(buffer, data[0], data+n, str_len, large, 0);
      
    } // opaque
  }
  return false;
} 

