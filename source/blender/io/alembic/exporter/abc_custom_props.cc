/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2020 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup Alembic
 */

#include "abc_custom_props.h"

#include <functional>
#include <iostream>
#include <memory>
#include <string>

#include <Alembic/Abc/OTypedArrayProperty.h>
#include <Alembic/Abc/OTypedScalarProperty.h>

#include "BKE_idprop.h"
#include "DNA_ID.h"

using Alembic::Abc::ArraySample;
using Alembic::Abc::OArrayProperty;
using Alembic::Abc::OBoolArrayProperty;
using Alembic::Abc::ODoubleArrayProperty;
using Alembic::Abc::OFloatArrayProperty;
using Alembic::Abc::OInt32ArrayProperty;
using Alembic::Abc::OStringArrayProperty;

namespace blender::io::alembic {

CustomPropertiesExporter::CustomPropertiesExporter(
    Alembic::Abc::OCompoundProperty abc_compound_prop, uint32_t timesample_index)
    : abc_compound_prop_(abc_compound_prop), timesample_index_(timesample_index)
{
}

CustomPropertiesExporter::~CustomPropertiesExporter()
{
}

void CustomPropertiesExporter::write_all(IDProperty *group)
{
  if (group == nullptr) {
    std::cout << "CustomPropertiesExporter::write_all(nullptr)\n";
    return;
  }
  std::cout << "CustomPropertiesExporter::write_all()\n";
  BLI_assert(group->type = IDP_GROUP);

  /* Loop over the properties, just like IDP_foreach_property() does but without the recursion. */
  LISTBASE_FOREACH (IDProperty *, id_property, &group->data.group) {
    if (STREQ(id_property->name, "_RNA_UI")) {
      continue;
    }
    write(id_property);
  }

  std::cout << "\n";
}

void CustomPropertiesExporter::write(IDProperty *id_property)
{
  std::cout << "    CustomPropertiesExporter::write(" << id_property->name << " ";

  if (id_property->name[0] == '\0') {
    strcpy(id_property->name, "NONAME");
    std::cout << "\033[95mskip nameless of type " << id_property->type << "\033[0m)\n";
    /* This happens when custom properties are pointing to things like mathutils.Matrix. Not sure
     * why they don't get a name then, though. */
    return;
  }

  switch (id_property->type) {
    case IDP_STRING: {
      std::cout << "string";
      /* The Alembic library doesn't accept NULL-terminated character arrays. */
      const std::string prop_value(IDP_String(id_property), id_property->len - 1);
      set_scalar_property<OStringArrayProperty, std::string>(id_property->name, prop_value);
      break;
    }
    case IDP_INT:
      std::cout << "int";
      static_assert(sizeof(int) == sizeof(int32_t), "Expecting 'int' to be 32-bit");
      set_scalar_property<OInt32ArrayProperty, int32_t>(id_property->name, IDP_Int(id_property));
      break;
    case IDP_FLOAT:
      std::cout << "float";
      set_scalar_property<OFloatArrayProperty, float>(id_property->name, IDP_Float(id_property));
      break;
    case IDP_DOUBLE:
      std::cout << "double (" << IDP_Double(id_property) << ")";
      set_scalar_property<ODoubleArrayProperty, double>(id_property->name,
                                                        IDP_Double(id_property));
      break;
    case IDP_ARRAY:
      write_array(id_property);
      break;
    case IDP_IDPARRAY:
      write_idparray(id_property);
      break;

    /* Unsupported: */
    case IDP_GROUP:
    case IDP_ID:
      std::cout << "\033[91munsupported type " << (uint16_t)id_property->type << "\033[0m)\n";
      break;
  }
}

void CustomPropertiesExporter::write_array(IDProperty *id_property)
{
  BLI_assert(id_property->type == IDP_ARRAY);

  switch (id_property->subtype) {
    case IDP_INT: {
      std::cout << "int[]";
      const int *array = (int *)IDP_Array(id_property);
      static_assert(sizeof(int) == sizeof(int32_t), "Expecting 'int' to be 32-bit");
      set_array_property<OInt32ArrayProperty, int32_t>(id_property->name, array, id_property->len);
      break;
    }
    case IDP_FLOAT: {
      std::cout << "float[]";
      const float *array = (float *)IDP_Array(id_property);
      set_array_property<OFloatArrayProperty, float>(id_property->name, array, id_property->len);
      break;
    }
    case IDP_DOUBLE: {
      std::cout << "double[]";
      const double *array = (double *)IDP_Array(id_property);
      set_array_property<ODoubleArrayProperty, double>(id_property->name, array, id_property->len);
      break;
    }

    /* Unsupported: */
    case IDP_ARRAY:
      std::cout << "\033[91marray-of-arrays\033[0m\n";
      break;
    case IDP_GROUP:
      std::cout << "\033[91marray-of-groups\033[0m\n";
      break;
    case IDP_ID:
      std::cout << "\033[91marray-of-IDs\033[0m\n";
      break;
    case IDP_IDPARRAY:
      std::cout << "\033[91array-of-IDPARRAY\033[0m)\n";
      break;
  }
}

void CustomPropertiesExporter::write_idparray(IDProperty *idp_array)
{
  BLI_assert(idp_array->type == IDP_IDPARRAY);

  if (idp_array->len == 0) {
    /* Don't bother writing dataless arrays. */
    return;
  }

  IDProperty *idp_elements = (IDProperty *)IDP_Array(idp_array);

#ifndef NDEBUG
  /* Sanity check that all elements of the array have the same type.
   * Blender should already enforce this, hence it's only used in debug mode. */
  for (int i = 1; i < idp_array->len; i++) {
    if (idp_elements[i].type == idp_elements[0].type) {
      continue;
    }

    std::cerr << "Custom property " << idp_array->name << " has elements of varying type";
    BLI_assert(!"Mixed type IDP_ARRAY custom property found");
  }
#endif

  // /* Loop over the array, just like IDP_foreach_property() does but without the recursion. */
  // IDProperty *loop = IDP_Array(idp_array);
  // for (int i = 0; i < idp_array->len; i++) {
  //   IDP_foreach_property(&loop[i], type_filter, callback, user_data);
  // }

  std::cout << "IDPArray of ";
  switch (idp_elements[0].type) {
    case IDP_STRING:
      std::cout << "IDP_STRING";
      write_idparray_of_strings(idp_array);
      break;
    case IDP_ARRAY:
      std::cout << "IDP_ARRAY: ";
      write_idparray_matrix(idp_array);
      break;

    /* Unsupported. */
    case IDP_INT:
    case IDP_FLOAT:
    case IDP_DOUBLE:
    case IDP_GROUP:
    case IDP_ID:
    case IDP_IDPARRAY:
    case IDP_NUMTYPES:
      break;
  }
  std::cout << "\n";
}

void CustomPropertiesExporter::write_idparray_of_strings(IDProperty *idp_array)
{
  BLI_assert(idp_array->type == IDP_IDPARRAY);
  BLI_assert(idp_array->len > 0);

  /* Convert to an array of std::strings, because Alembic doesn't like zero-delimited strings. */
  IDProperty *idp_elements = (IDProperty *)IDP_Array(idp_array);
  std::vector<std::string> strings(idp_array->len);
  for (int i = 0; i < idp_array->len; i++) {
    BLI_assert(idp_elements[i].type == IDP_STRING);
    strings[i] = IDP_String(&idp_elements[i]);
  }

  /* Alembic needs a pointer to the first value of the array. */
  const std::string *array_of_strings = &strings[0];
  set_array_property<OStringArrayProperty, std::string>(
      idp_array->name, array_of_strings, strings.size());
}

void CustomPropertiesExporter::write_idparray_matrix(IDProperty *idp_array)
{
  BLI_assert(idp_array->type == IDP_IDPARRAY);
  BLI_assert(idp_array->len > 0);

  /* This must be an array of arrays. */
  IDProperty *idp_rows = (IDProperty *)IDP_Array(idp_array);
  BLI_assert(idp_rows[0].type == IDP_ARRAY);

  /* This function is made for writing NxM matrices, and doesn't support non-numeric types or rows
   * of varying length. */
  const size_t num_rows = idp_array->len;
  if (num_rows == 0) {
    return;
  }
  const size_t num_cols = idp_rows[0].len;
  const int subtype = idp_rows[0].subtype;
  if (!ELEM(subtype, IDP_INT, IDP_FLOAT, IDP_DOUBLE)) {
    /* Non-numerical types are not supported. */
    return;
  }

  /* All rows must have the same length, and have values of the same type. */
  for (int row_idx = 0; row_idx < num_rows; row_idx++) {
    const IDProperty &idp_row = idp_rows[0];
    if (idp_row.subtype != subtype || idp_row.len != num_cols) {
      return;
    }
  }

  std::cout << "matrix[" << num_rows << "][" << num_cols << "]";
  switch (subtype) {
    case IDP_INT:
      write_idparray_matrix_int(idp_array);
      break;
    case IDP_FLOAT:
      write_idparray_matrix_float(idp_array);
      break;
    case IDP_DOUBLE:
      write_idparray_matrix_double(idp_array);
      break;
  }
}

void CustomPropertiesExporter::write_idparray_matrix_int(IDProperty * /*idp_array*/)
{
  std::cout << "int";
}

void CustomPropertiesExporter::write_idparray_matrix_float(IDProperty * /*idp_array*/)
{
  std::cout << "float";
}

void CustomPropertiesExporter::write_idparray_matrix_double(IDProperty *idp_array)
{
  std::cout << "double";
  BLI_assert(idp_array->type == IDP_IDPARRAY);
  BLI_assert(idp_array->len > 0);

  /* This must be an array of arrays of double. */
  const IDProperty *idp_rows = (IDProperty *)IDP_Array(idp_array);
  BLI_assert(idp_rows[0].type == IDP_ARRAY);
  BLI_assert(idp_rows[0].len > 0);
  BLI_assert(idp_rows[0].subtype == IDP_DOUBLE);

  const uint64_t num_rows = idp_array->len;
  const uint64_t num_cols = idp_rows[0].len;
  std::unique_ptr<double[]> matrix_values = std::make_unique<double[]>(num_rows * num_cols);

  size_t matrix_idx = 0;
  for (size_t row_idx = 0; row_idx < num_rows; ++row_idx, matrix_idx += num_cols) {
    const double *row = (double *)IDP_Array(&idp_rows[row_idx]);
    memcpy(&matrix_values[matrix_idx], row, sizeof(double) * num_cols);
  }

  Alembic::Util::Dimensions array_dimensions;
  array_dimensions.setRank(2);
  array_dimensions[0] = num_rows;
  array_dimensions[1] = num_cols;

  set_array_property<ODoubleArrayProperty, double>(
      idp_array->name, matrix_values.get(), array_dimensions);
}

template<typename ABCPropertyType, typename BlenderValueType>
void CustomPropertiesExporter::set_scalar_property(const StringRef property_name,
                                                   const BlenderValueType property_value)
{
  set_array_property<ABCPropertyType, BlenderValueType>(property_name, &property_value, 1);
}

template<typename ABCPropertyType, typename BlenderValueType>
void CustomPropertiesExporter::set_array_property(const StringRef property_name,
                                                  const BlenderValueType *array_values,
                                                  const size_t num_array_items)
{
  Alembic::Util::Dimensions array_dimensions(num_array_items);
  set_array_property<ABCPropertyType, BlenderValueType>(
      property_name, array_values, array_dimensions);
}

template<typename ABCPropertyType, typename BlenderValueType>
void CustomPropertiesExporter::set_array_property(const StringRef property_name,
                                                  const BlenderValueType *array_values,
                                                  const Alembic::Util::Dimensions &dimensions)
{
  std::cout << " " << array_values[0] << ")\n";

  /* Create an Alembic property if it doesn't exist yet. */
  auto create_callback = [this, property_name]() -> OArrayProperty {
    ABCPropertyType abc_property(abc_compound_prop_, property_name);
    abc_property.setTimeSampling(timesample_index_);
    return abc_property;
  };
  OArrayProperty array_prop = abc_properties_.lookup_or_add_cb(property_name, create_callback);
  ArraySample sample(array_values, array_prop.getDataType(), dimensions);
  array_prop.set(sample);
}

}  // namespace blender::io::alembic
