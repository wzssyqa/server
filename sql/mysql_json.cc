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

bool parse_value(String *buffer, size_t type, const char *data, size_t len)
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
    return false; //return parse_mysql_scalar(type, data, len); // ovo ne radi
  }
}

bool parse_array_or_object(String *buffer,Field_mysql_json::enum_type t, const char *data,                                size_t len, bool large)
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
  size_t header_size= 2 * offset_size;
  if (t==Field_mysql_json::enum_type::OBJECT)
    header_size+= element_count *
      (large ? KEY_ENTRY_SIZE_LARGE : KEY_ENTRY_SIZE_SMALL);
  header_size+= element_count *
    (large ? VALUE_ENTRY_SIZE_LARGE : VALUE_ENTRY_SIZE_SMALL);

  // The header should not be larger than the full size of the value.
  if (header_size > bytes)
    return true;                             /* purecov: inspected */


  if (t==Field_mysql_json::enum_type::OBJECT)
  {
    size_t key_json_offset, key_json_start, key_json_len;
    size_t value_type_offset, value_counter(0);

    bool is_last(false);
    if(buffer->append('{'))
      return true;
    
    char *key_element;
    for (uint i=0; i<element_count; i++)
    {
      
      key_json_offset= 2*offset_size+i*(large?KEY_ENTRY_SIZE_LARGE:KEY_ENTRY_SIZE_SMALL);
      key_json_start= read_offset_or_size(data+key_json_offset,large);
      key_json_len= read_offset_or_size(data+key_json_offset+offset_size, large);
      
      key_element= new char[key_json_len+1];
      memmove(key_element, const_cast<char*>(&data[key_json_start]),                    key_json_len);
      key_element[key_json_len]= '\0';

      // @anel method: String::apppend(const char*) is not working 
      //keys->append((const char*)key_element); // generating error
      //keys->append(STRING_WITH_LEN(key_element));

      if(buffer->append('"'))
        return true;
      if( buffer->append(String((const char *)key_element, &my_charset_bin)) )
        return true;
      if(buffer->append('"'))
        return true;
      delete[] key_element;
      if(buffer->append(":"))
        return true;
      // @anel calculate value type and get a value
      value_type_offset= 2*offset_size+ 
      (large?KEY_ENTRY_SIZE_LARGE:KEY_ENTRY_SIZE_SMALL)*(element_count)+
      (large ? VALUE_ENTRY_SIZE_LARGE : VALUE_ENTRY_SIZE_SMALL)*value_counter;
      value_counter++;

      if(i==(element_count-1))
        is_last=true;

      if(parse_mysql_scalar(buffer, value_type_offset, data, is_last, large, 0))
      {
        return true;
      }
      is_last=false;
    }

     if(buffer->append('}'))
      return true;
  }
  else
  {
    // Parsing Field_mysql_json::enum_type::ARRAY
    if(buffer->append("Array-!"))
      return true;
  }
  

  return false;
}

bool parse_mysql_scalar(String* buffer, size_t value_type_offset, const char *data, bool is_last, bool large, size_t depth)
{
  size_t value_json_type;

  if (check_json_depth(++depth))
    return true;

  value_json_type= data[value_type_offset];
  switch(value_json_type)
  {
    /** FINISHED WORKS **/
    case JSONB_TYPE_INT16 :
    {
      buffer->append_longlong((longlong) (sint2korr(data+value_type_offset+1)));
      break;
    }

    /** FINISHED WORKS **/
    case JSONB_TYPE_INT32:
    {
      // depending on large -> can be inline or not-inline value @todo test
      if(!large)
      {
        // In this case value_length_ptr is start of the value and has 4 bytes -> mysql is checking the length which is sent  and raise an error (bytes-value_length_pt)
        size_t value_length_ptr;
        char *value_element;
        uint num_bytes=8;
        value_element= new char[num_bytes+1];
        value_length_ptr= read_offset_or_size(data+value_type_offset+1, large);
        memmove(value_element, const_cast<char*>(&data[value_length_ptr]), num_bytes);
        value_element[num_bytes+1]= '\0';
        if( buffer->append_longlong(sint4korr(value_element)))
          return true;
        delete[] value_element;
        break;
      } 
      else
      {
        if( buffer->append_longlong(sint4korr(data+value_type_offset+1)))
          return true;
      }
      break;
    }

    /** @todo Need to test ! **/
    case JSONB_TYPE_UINT16 : 
    {
      buffer->append_longlong((longlong) (uint2korr(data+value_type_offset+1)));
      break;
    }

    /*** NOT WORKING ****/
    case JSONB_TYPE_UINT64:
    {
      // depending on large -> can be inline or not-inline value @todo test
      if(!large)
      {
        // In this case value_length_ptr is start of the value and has 8 bytes -> mysql is checking the length which is sent  and raise an error (bytes-value_length_pt)
        size_t value_length_ptr;
        char *value_element;
        uint num_bytes=8;

        value_element= new char[num_bytes+1];
        value_length_ptr= read_offset_or_size(data+value_type_offset+1, large);
        memmove(value_element, const_cast<char*>(&data[value_length_ptr]),num_bytes);
        value_element[num_bytes+1]= '\0';
        if( buffer->append_longlong(uint8korr(value_element)))
          return true;
        delete[] value_element;
        break;
      }
      else
      {
        if( buffer->append_longlong(uint8korr(data+value_type_offset+1)))
          return true;
      }
      break;
    }
    /** FINISHED WORKS **/
    case JSONB_TYPE_STRING:
    {
      size_t value_length, value_length_ptr;
      char *value_element;
      value_length_ptr= read_offset_or_size(data+value_type_offset+1, large);
      value_length= (uint) data[value_length_ptr];
      value_element= new char[value_length+1];
      memmove(value_element, const_cast<char*>(&data[value_length_ptr+1]),              value_length);
      value_element[value_length]= '\0';
      if(buffer->append('"'))
        return true;
      if( buffer->append(String((const char *)value_element, &my_charset_bin)) )
        return true;
      if(buffer->append('"'))
        return true;
      delete[] value_element;
    }
  }

  if(!is_last)
  {
    buffer->append(",");
  }

  return false;
} 