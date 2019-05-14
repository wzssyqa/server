#include "mariadb.h"
#include "sql_base.h"

/**
  @file
   Check if SELECT list and HAVING clause items are determined.
   Item is called determined in this context if it is used in
   GROUP BY or SELECT DISTINCT or is functionally dependent
   on GROUP BY or SELECT DISTINCT fields.
   Only determined fields can be used in SELECT LIST and HAVING.

   Item is called functionally dependent on some field
   if it can be got by applying some function to this field and such
   a rule holds:
   if two values of this field are equal (or both NULL)
   then two corresponding values of functionally dependent
   item are also equal or both NULL.

   If there are any SELECT list or HAVING items that are
   not found to be determined then WHERE clause equalities
   are checked. From these equalities new fields eq_fields that
   are equal with determined fields can be extracted.

   If SELECT list or HAVING items are in eq_fields list or are
   functionally dependent on eq_fields they are said to be
   determined.

   Work if 'only_full_group_by' mode is set only.
*/


/**
  Check if all key parts of 'key' are determined fields.
  If so return true.
*/

static bool are_key_parts_determined(KEY *key)
{
  Item *item_arg= NULL;
  for (uint i= 0; i < key->user_defined_key_parts; ++i)
  {
    if (!key->key_part[i].field->
          excl_func_dep_on_grouping_fields(&item_arg))
      return false;
  }
  return true;
}


/**
  @brief
    Check if either PRIMARY key or UNIQUE key fields are determined

  @param
    join_list  list of tables used in FROM clause

  @details
    For each table used in JOIN check if its PRIMARY key or UNIQUE
    keys fields are determined. If all fields of the PRIMARY key or
    some UNIQUE key of some table are determined then according
    to SQL Standard all fields of this table are said to be determined.

  @retval
    false  if new determined fields were found
    true   otherwise
*/

static
bool check_unique_keys_determined(List<TABLE_LIST> *join_list)
{
  List_iterator<TABLE_LIST> it(*join_list);
  TABLE_LIST *tbl;
  bool no_fields_extracted= true;
  while ((tbl= it++))
  {
    /* Check if all fields of this table are already found to be determined. */
    if (bitmap_is_set_all(&tbl->table->tmp_set))
      continue;
    /*
      Check PRIMARY key fields if they are determined
      if there is any PRIMARY key.
    */
    if (tbl->table->s->primary_key < MAX_KEY)
    {
      KEY *pk= &tbl->table->key_info[tbl->table->s->primary_key];
      if (are_key_parts_determined(pk))
        bitmap_set_all(&tbl->table->tmp_set);
    }
    if (!bitmap_is_set_all(&tbl->table->tmp_set))
    {
      /*
        Check UNIQUE keys fields if they are determined
        if there are any UNIQUE keys.
      */
      KEY *end= tbl->table->key_info + tbl->table->s->keys;
      for (KEY *k= tbl->table->key_info; k < end; k++)
        if ((k->flags & HA_NOSAME) && are_key_parts_determined(k))
        {
          bitmap_set_all(&tbl->table->tmp_set);
          break;
        }
    }
    if (bitmap_is_set_all(&tbl->table->tmp_set))
      no_fields_extracted= false;
  }
  return no_fields_extracted;
}


/**
  @brief
    Collect fields used in GROUP BY or in SELECT DISTINCT.

  @param
    join_list        list of tables used in FROM clause
    select_distinct  true if SELECT DISTINCT construction is used

  @details
    For each table used in JOIN list collect fields used in GROUP BY
    as determined fields. Save them in tmp_set map.
    If SELECT DISTINCT construction is used collect all fields
    used in SELECT list as determined.
    Check if there are any PRIMARY key or UNIQUE keys which
    fields are determined with check_unique_keys_determined() method.

  @retval
    false  if there is neither GROUP BY nor SELECT DISTINCT construction
    true   otherwise
*/

bool st_select_lex::collect_determined_fields(List<TABLE_LIST> *join_list,
                                              bool select_distinct)
{
  if (!select_distinct && (group_list.elements == 0))
    return false;

  List_iterator<TABLE_LIST> it(*join_list);
  TABLE_LIST *tbl;
  while ((tbl= it++))
  {
    bitmap_clear_all(&tbl->table->tmp_set);
  }

  /* Collect SELECT list fields if DISTINCT is used */
  if (select_distinct)
  {
    List_iterator<Item> li(item_list);
    Item *item;
    while ((item=li++))
    {
      if (item->type() == Item::FIELD_ITEM)
      {
        bitmap_set_bit(&((Item_field *)item)->field->table->tmp_set,
                       ((Item_field *)item)->field->field_index);
      }
    }
  }

  /* Collect GROUP BY fields */
  for (ORDER *ord= group_list.first; ord; ord= ord->next)
  {
    Item *ord_item= *ord->item;
    if (ord_item->type() == Item::FIELD_ITEM)
    {
      bitmap_set_bit(&((Item_field *)ord_item)->field->table->tmp_set,
                     ((Item_field *)ord_item)->field->field_index);
    }
  }

  /*
    Check if PRIMARY or UNIQUE keys are determined.
    If so, all fields of these keys tables are determined.
  */
  check_unique_keys_determined(join_list);
  return true;
}


/**
  Check if all items of HAVING clause are determined.
*/

bool is_having_clause_determined(Item *having)
{
  if (!having)
    return true;

  Item *item_arg= NULL;
  if (having->excl_func_dep_on_grouping_fields(&item_arg))
    return true;
  // assert item_arg null
  my_error(ER_NO_FUNCTIONAL_DEPENDENCE_ON_GROUP_BY, MYF(0),
           item_arg->full_name());
  return false;
}


/**
  Check if all SELECT list items are determined.
*/

bool st_select_lex::is_select_list_determined(bool select_distinct)
{
  if (select_distinct)
    return true;

  Item *item;
  List_iterator<Item> li(item_list);
  while ((item=li++))
  {
    Item *item_arg= NULL;
    if (item->real_item()->type() == Item::FIELD_ITEM &&
        item->used_tables() & OUTER_REF_TABLE_BIT)
      continue;
    if (item->excl_func_dep_on_grouping_fields(&item_arg))
      continue;
    if (item->type() != Item::FIELD_ITEM)
    {
      // will be removed
      ORDER *ord;
      for (ord= group_list.first; ord; ord= ord->next)
        if ((*ord->item)->eq(item, 0))
          break;
      if (ord)
        continue;
    }
    my_error(ER_NO_FUNCTIONAL_DEPENDENCE_ON_GROUP_BY, MYF(0),
             item_arg->full_name());
    return false;
  }
  return true;
}


static void free_context(List<Item::Context> *ctx)
{
  List_iterator<Item::Context> li(*ctx);
  Item::Context *ctx_item;

  while ((ctx_item=li++))
    free(ctx_item);
}


/**
  @brief
    Check using contexts information if field is determined

  @param
    dep_contexts  list of Contexts of determined fields which stand
                  on the one hand WHERE clause equality
    nd_contexts   list of Contexts of fields which stand on the other
                  side of the equality (the opposite side from
                  dep_contexts fields).
    nd_field      non-determined field which Context is in the tail
                  of nd_contexts

  @details
    This method checks if nd_field marked as determined doesn’t not lead to a
    non-deterministic result.

    For example, consider query Q1:

    SELECT LENGTH(a)
    FROM t
    WHERE a=b
    GROUP BY b;

    where a and b are of VARCHAR type and
    a = '  x'
    b = 'x'

    Let's substitute in WHERE equality a and b values:
    '  x'='x' will be true for VARCHAR fields.

    but a and b lengths are different:
    (LENGTH(a) = 3) != (LENGTH(b) = 1)

    So from a = b doesn't follow that LENGTH(a) = LENGTH(b)
    and LENGTH(a) can't be used in SELECT list.

    To prevent such situations can_be_substituted_to_equal_item()
    is called. It checks if nd_field context doesn't conflict
    with dep_contexts.

  @comments
    Initial equality from which arguments of this method are extracted
    should be of the form:

    g1(f11, ..., f1n) = g2(f21, ..., f2n)      (1)

    where f11, ..., f1n are determined fields and
          f21, ..., f2n are non-determined fields.

    If there is more than one field in f21, ..., f2n it can't be said
    that these fields are determined due to the implementation restrictions.
    To check it the count of nd_contexts list elements is checked.

    So, equality (1) should be of the form:

    g1(f11, ..., f1n) = g2(f2)                (2)

  @retval
    false  if nd_field is found to be non-determined
    true   otherwise
*/


static
bool extract_new_func_dep(List<Item::Context> dep_contexts,
                          List<Item::Context> nd_contexts,
                          Field *nd_field)
{
  if (nd_contexts.elements > 1)
    return false;
  Type_handler_hybrid_field_type
    tmp(dep_contexts.head()->compare_type_handler());
  List_iterator<Item::Context> it(dep_contexts);
  Item::Context *ctx;
  it++;
  while ((ctx=it++))
    if (tmp.aggregate_for_comparison(ctx->compare_type_handler()))
      return false;
  it.rewind();
  while ((ctx=it++))
    if (!nd_field->can_be_substituted_to_equal_item(*nd_contexts.head(),
                 Item::Context(ctx->subst_constraint(),
                               tmp.type_handler(),
                               ctx->compare_collation())))
      return false;
  /* Mark nd_field as determined */
  bitmap_set_bit(&nd_field->table->tmp_set, nd_field->field_index);
  return true;
}


/**
  @brief
    Find new determined fields from WHERE clause equalities

  @param
    thd         thread handle
    cond        WHERE clause
    join_list   list of tables used in FROM clause

  @details
    This method checks if there are any equalities in WHERE clause
    and any new determined fields can be extracted from these equalities.

    Field that is equal to a determined field or a function from determined
    fields is also determined with respect to types.

    The method is divided into several steps:

    1. Collect WHERE clause equalities
    2. Until no new fields can be extracted loop through equalities:
       a. Check if one side of the equality depends on determined fields only and
          the other depends on non-determined fields only.
       b. Check items types and contexts
       c. Mark found non-determined field as determined
       d. Delete equality

  @retval
    false  if no error occurs
    true   true
*/

static bool get_func_dep_from_conds(THD *thd, Item *cond,
                                    List<TABLE_LIST> *join_list)
{
  if (!cond)
    return false;

  /* 1. Collect WHERE clause equalities */
  List<Item_func_eq> cond_equalities;
  if (cond && cond->type() == Item::COND_ITEM &&
      ((Item_cond*) cond)->functype() == Item_func::COND_AND_FUNC)
  {
    List_iterator_fast<Item> li(*((Item_cond*) cond)->argument_list());
    Item *item;
    while ((item=li++))
    {
      if (item->type() == Item::FUNC_ITEM &&
          ((Item_func*) item)->functype() == Item_func::EQ_FUNC &&
          cond_equalities.push_back((Item_func_eq *)item, thd->mem_root))
        return true;
    }
  }
  else if (cond->type() == Item::FUNC_ITEM &&
           ((Item_func*) cond)->functype() == Item_func::EQ_FUNC &&
           cond_equalities.push_back((Item_func_eq *)cond, thd->mem_root))
    return true;

  if (!cond_equalities.elements)
    return false;

  List_iterator<Item_func_eq> li(cond_equalities);
  Item_func_eq *eq_item;
  bool new_fields_extracted= true;
  List<Item::Context> nd_contexts;

  /* 2. Until no new fields can be extracted loop through equalities: */
  while (new_fields_extracted && cond_equalities.elements > 0)
  {
    new_fields_extracted= false;
    while ((eq_item=li++))
    {
      Item *item1= eq_item->arguments()[0];
      Field *field_arg1= NULL;
      List<Item::Context> contexts1;
      Item *item2= eq_item->arguments()[1];
      Field *field_arg2= NULL;
      List<Item::Context> contexts2;

      Item::Context ctx1(Item::IDENTITY_SUBST,
                         item1->type_handler_for_comparison(),
                         item1->collation.collation);
      Item::Context ctx2(Item::IDENTITY_SUBST,
                         item2->type_handler_for_comparison(),
                         item2->collation.collation);
      /*
        a. Check if one side of the equality depends on determined fields
           only and the other depends on non-determined fields only.
      */
      bool dep1= item1->excl_func_dep_in_equalities(thd, &contexts1,
                                                   ctx1, &field_arg1);
      /*
        If item1 is constant delete equality as no new fields can
        be extracted from it.
      */
      if (!field_arg1)
      {
        free_context(&contexts1);
        li.remove();
        continue;
      }

      bool dep2= item2->excl_func_dep_in_equalities(thd, &contexts2,
                                                    ctx2, &field_arg2);
      /*
        If item2 is constant or equality depends on determined fields
        only delete equality.
      */
      if ((!field_arg2) || (dep1 && dep2))
      {
        free_context(&contexts1);
        free_context(&contexts2);
        li.remove();
        continue;
      }
      if (!dep1 && !dep2)
      {
        free_context(&contexts1);
        free_context(&contexts2);
        continue;
      }
      /*
        b. Check items types and contexts
        c. Mark found non-determined field as determined
      */
      if ((item1->type_handler_for_comparison() ==
           eq_item->compare_type_handler() &&
           item2->type_handler_for_comparison() ==
           eq_item->compare_type_handler()) &&
           ((dep1 &&
           extract_new_func_dep(contexts1, contexts2, field_arg2)) ||
           (dep2 &&
           extract_new_func_dep(contexts2, contexts1, field_arg1))))
        new_fields_extracted= true;
      free_context(&contexts1);
      free_context(&contexts2);
      /* d. Delete equality */
      li.remove();
    }
    /*
      Check unique and primary keys if their fields become determined
    */
    if ((!new_fields_extracted || cond_equalities.elements == 0) &&
        !check_unique_keys_determined(join_list))
      new_fields_extracted= true;
    li.rewind();
  }
  return false;
}


/**
  @brief
    Check if all SELECT list and HAVING items are determined

  @param
    thd              thread handle
    join_list        list of tables used in FROM clause
    select_distinct  true if SELECT DISTINCT construction is used
    cond             WHERE clause
    having           HAVING clause

  @details
    This method finds determined fields (items that are used in GROUP
    BY or SELECT DISTINCT list and are allowed to be used in SELECT list
    and HAVING clause).
    It also finds fields that are equal to determined fields in WHERE clause
    equalities. These fields are also said to be determined.
    Finally, it checks if SELECT LIST (except a case when SELECT DISTINCT
    is used) and HAVING clause items depend on determined items only.

  @retval
    false  if no error occurs
    true   true
*/

bool
st_select_lex::check_func_dependencies(THD *thd,
                                       List<TABLE_LIST> *join_list,
                                       bool select_distinct,
                                       Item *cond,
                                       Item *having)
{
  /* Stop if no tables used */
  if (!join_list->elements)
    return false;

  /* Collect fields from GROUP BY and/or SELECT DISTINCT list*/
  if (!collect_determined_fields(join_list, select_distinct))
    return false;

  /* Try to find new fields that are equal to determined*/
  if (get_func_dep_from_conds(thd, cond, join_list))
    return true;

  /*
    Check if SELECT list and HAVING clause consists of items that
    are determined or functionally dependent on determined fields.
  */
  if (!is_select_list_determined(select_distinct) ||
      !is_having_clause_determined(having))
    return true;

  return false;
}
