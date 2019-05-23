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



bool parse_array_or_object(Field_mysql_json::enum_type t, const char *data,                                size_t len, bool large, size_t *const bytes,
                           size_t *const element_count)
{
  //DBUG_ASSERT(t == Field_mysql_json::ARRAY || t == Field_mysql_json::OBJECT);
  /*
    Make sure the document is long enough to contain the two length fields
    (both number of elements or members, and number of bytes).
  */
  const size_t offset_size= large ? LARGE_OFFSET_SIZE : SMALL_OFFSET_SIZE;
  if (len < 2 * offset_size)
    return true;

  // Calculate values of interest
  *element_count= read_offset_or_size(data, large);
  *bytes= read_offset_or_size(data + offset_size, large);

  // The value can't have more bytes than what's available in the data buffer.
  if (*bytes > len)
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
    header_size+= *element_count *
      (large ? KEY_ENTRY_SIZE_LARGE : KEY_ENTRY_SIZE_SMALL);
  header_size+= *element_count *
    (large ? VALUE_ENTRY_SIZE_LARGE : VALUE_ENTRY_SIZE_SMALL);

  // The header should not be larger than the full size of the value.
  if (header_size > *bytes)
    return true;                             /* purecov: inspected */


  if (t==Field_mysql_json::enum_type::OBJECT)
  {
    size_t key_json_offset, key_json_start, key_json_len;
    size_t value_type_offset, value_counter(0);
    size_t value_length, value_length_ptr;
    //size_t value_json_type;
    //String *buffer=(String *)malloc(sizeof(String)); // @anel v1
    String *buffer= new String(); // @anel v2 which construction ?

    char *key_element, *value_element;  
    for (uint i=0; i<*element_count; i++)
    {
      
      key_json_offset= 2*offset_size+i*(large?KEY_ENTRY_SIZE_LARGE:KEY_ENTRY_SIZE_SMALL);
      key_json_start= read_offset_or_size(data+key_json_offset,large);
      key_json_len= read_offset_or_size(data+key_json_offset+offset_size, large);
      
      //key_element=(char *)malloc(sizeof(char)*key_json_len+1); // v1
      key_element= new char[key_json_len+1]; // v2
      memmove(key_element, const_cast<char*>(&data[key_json_start]),                    key_json_len);
      key_element[key_json_len]= '\0';

    // @anel method: String::apppend(const char*) is not working 
    //keys->append((const char*)key_element); // generating error
    //keys->append(STRING_WITH_LEN(key_element));

      buffer->append(String((const char *)key_element, &my_charset_bin));
      delete key_element;

      // @anel calculate value type and get a value
      value_type_offset= 2*offset_size+ (large?KEY_ENTRY_SIZE_LARGE:KEY_ENTRY_SIZE_SMALL)*(*element_count)+(large ? VALUE_ENTRY_SIZE_LARGE : VALUE_ENTRY_SIZE_SMALL)*value_counter;
     // value_json_type= data[value_type_offset];
      value_counter++;

      // this will go in separete function and depens on value_json_type
      // For now value_json_type=12(0xc) is json_object
      value_length_ptr= read_offset_or_size(data+value_type_offset+1, large);
      value_length= (uint) data[value_length_ptr];
      value_element= new char[value_length+1];
      memmove(value_element, const_cast<char*>(&data[value_length_ptr+1]),              value_length);
      value_element[value_length]= '\0';
      buffer->append(":");
      buffer->append(String((const char *)value_element, &my_charset_bin));
      delete value_element;
      if(i!=(*element_count-1))
        buffer->append(",");
    }
  }

  return 1;
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

bool get_mysql_string(String* buffer, size_t type, const char *data, size_t len,
                      bool large, size_t element_count, size_t bytes,
                      const char *func_name, size_t depth)
{
  if (check_json_depth(++depth))
    return true;
  switch(type)
  {
    case JSONB_TYPE_STRING:
    {
      if (buffer->append('{'))
        return true;                           /* purecov: inspected */
      break;
      // Implement iter.elt().first/second
    }
  }

  return false;
}