/**
 * NEURON IIoT System for Industry 4.0
 * Copyright (C) 2020-2022 EMQ Technologies Co., Ltd All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 **/
#include <memory.h>

#include <neuron.h>

#include "modbus_point.h"

struct modbus_sort_ctx {
    uint16_t start;
    uint16_t end;
};

static __thread uint16_t modbus_read_max_byte = 250;

static int  tag_cmp(neu_tag_sort_elem_t *tag1, neu_tag_sort_elem_t *tag2);
static bool tag_sort(neu_tag_sort_t *sort, void *tag, void *tag_to_be_sorted);
static int  tag_cmp_write(neu_tag_sort_elem_t *tag1, neu_tag_sort_elem_t *tag2);
static bool tag_sort_write(neu_tag_sort_t *sort, void *tag,
                           void *tag_to_be_sorted);

int modbus_tag_to_point(const neu_datatag_t *tag, modbus_point_t *point,
                        modbus_address_base address_base)
{
    int      ret           = NEU_ERR_SUCCESS;
    uint32_t start_address = 0;
    ret                    = neu_datatag_parse_addr_option(tag, &point->option);
    if (ret != 0) {
        return NEU_ERR_TAG_ADDRESS_FORMAT_INVALID;
    }

    char area = 0;
    int  n    = sscanf(tag->address, "%hhu!%c%u", &point->slave_id, &area,
                   &start_address);
    if (n != 3) {
        return NEU_ERR_TAG_ADDRESS_FORMAT_INVALID;
    }

    if (start_address == 65536 && address_base == 0) {
        point->start_address = 65535;
    } else if (start_address == 0 && address_base == 1) {
        point->start_address = 0;
    } else {
        point->start_address = (uint16_t) start_address - address_base;
    }

    point->type = tag->type;

    switch (area) {
    case '0':
        point->area = MODBUS_AREA_COIL;
        break;
    case '1':
        point->area = MODBUS_AREA_INPUT;
        break;
    case '3':
        point->area = MODBUS_AREA_INPUT_REGISTER;
        break;
    case '4':
        point->area = MODBUS_AREA_HOLD_REGISTER;
        break;
    default:
        return NEU_ERR_TAG_ADDRESS_FORMAT_INVALID;
    }

    if (point->area == MODBUS_AREA_INPUT ||
        point->area == MODBUS_AREA_INPUT_REGISTER) {
        if ((tag->attribute & NEU_ATTRIBUTE_WRITE) == NEU_ATTRIBUTE_WRITE) {
            return NEU_ERR_TAG_ATTRIBUTE_NOT_SUPPORT;
        }
    }

    switch (point->area) {
    case MODBUS_AREA_INPUT:
    case MODBUS_AREA_COIL:
        if (point->type != NEU_TYPE_BIT) {
            return NEU_ERR_TAG_TYPE_NOT_SUPPORT;
        }
        if (point->option.bit.bit > 7) {
            return NEU_ERR_TAG_ADDRESS_FORMAT_INVALID;
        }
        break;
    case MODBUS_AREA_INPUT_REGISTER:
    case MODBUS_AREA_HOLD_REGISTER:
        if (point->type == NEU_TYPE_STRING &&
            point->option.string.length <= 0) {
            return NEU_ERR_TAG_ADDRESS_FORMAT_INVALID;
        }
        if (point->type == NEU_TYPE_BYTES && point->option.bytes.length <= 0) {
            return NEU_ERR_TAG_ADDRESS_FORMAT_INVALID;
        }
        if (point->type == NEU_TYPE_BIT && point->option.bit.bit > 15) {
            return NEU_ERR_TAG_ADDRESS_FORMAT_INVALID;
        }
        if (point->type == NEU_TYPE_BIT &&
            (tag->attribute & NEU_ATTRIBUTE_WRITE) == NEU_ATTRIBUTE_WRITE) {
            return NEU_ERR_TAG_ATTRIBUTE_NOT_SUPPORT;
        }
        break;
    }

    switch (point->type) {
    case NEU_TYPE_BIT:
    case NEU_TYPE_BOOL:
    case NEU_TYPE_INT8:
    case NEU_TYPE_UINT8:
    case NEU_TYPE_PTR:
        point->n_register = 1;
        break;
    case NEU_TYPE_UINT16:
    case NEU_TYPE_INT16:
    case NEU_TYPE_WORD:
        if (point->area == MODBUS_AREA_COIL ||
            point->area == MODBUS_AREA_INPUT) {
            ret = NEU_ERR_TAG_TYPE_NOT_SUPPORT;
        } else {
            point->n_register = 1;
        }
        break;
    case NEU_TYPE_UINT32:
    case NEU_TYPE_INT32:
    case NEU_TYPE_FLOAT:
    case NEU_TYPE_DWORD:
    case NEU_TYPE_TIME:
    case NEU_TYPE_DATA_AND_TIME:
        if (point->area == MODBUS_AREA_COIL ||
            point->area == MODBUS_AREA_INPUT) {
            ret = NEU_ERR_TAG_TYPE_NOT_SUPPORT;
        } else {
            point->n_register = 2;
        }
        break;
    case NEU_TYPE_UINT64:
    case NEU_TYPE_INT64:
    case NEU_TYPE_DOUBLE:
    case NEU_TYPE_LWORD:
        if (point->area == MODBUS_AREA_COIL ||
            point->area == MODBUS_AREA_INPUT) {
            ret = NEU_ERR_TAG_TYPE_NOT_SUPPORT;
        } else {
            point->n_register = 4;
        }
        break;
    case NEU_TYPE_STRING:
        if (point->area == MODBUS_AREA_COIL ||
            point->area == MODBUS_AREA_INPUT) {
            ret = NEU_ERR_TAG_TYPE_NOT_SUPPORT;
        } else {
            if (point->option.string.length > 127) {
                return NEU_ERR_TAG_ADDRESS_FORMAT_INVALID;
            }
            switch (point->option.string.type) {
            case NEU_DATATAG_STRING_TYPE_H:
            case NEU_DATATAG_STRING_TYPE_L:
                point->n_register = point->option.string.length / 2 +
                    point->option.string.length % 2;
                break;
            case NEU_DATATAG_STRING_TYPE_D:
            case NEU_DATATAG_STRING_TYPE_E:
                point->n_register = point->option.string.length;
                break;
            }
        }
        break;
    case NEU_TYPE_BYTES:
        if (point->area == MODBUS_AREA_COIL ||
            point->area == MODBUS_AREA_INPUT) {
            ret = NEU_ERR_TAG_TYPE_NOT_SUPPORT;
        } else {
            if (point->option.bytes.length > 128 ||
                point->option.bytes.length % 2 == 1) {
                return NEU_ERR_TAG_ADDRESS_FORMAT_INVALID;
            }
            point->n_register =
                point->option.bytes.length / 2 + point->option.bytes.length % 2;
        }
        break;
    default:
        return NEU_ERR_TAG_TYPE_NOT_SUPPORT;
    }

    strncpy(point->name, tag->name, sizeof(point->name));
    return ret;
}

int modbus_write_tag_to_point(const neu_plugin_tag_value_t *tag,
                              modbus_point_write_t *        point,
                              modbus_address_base           address_base)
{
    int ret      = NEU_ERR_SUCCESS;
    ret          = modbus_tag_to_point(tag->tag, &point->point, address_base);
    point->value = tag->value;
    return ret;
}

modbus_read_cmd_sort_t *modbus_tag_sort(UT_array *tags, uint16_t max_byte)
{
    modbus_read_max_byte          = max_byte;
    neu_tag_sort_result_t *result = neu_tag_sort(tags, tag_sort, tag_cmp);

    modbus_read_cmd_sort_t *sort_result =
        calloc(1, sizeof(modbus_read_cmd_sort_t));
    sort_result->n_cmd = result->n_sort;
    sort_result->cmd   = calloc(result->n_sort, sizeof(modbus_read_cmd_t));

    for (uint16_t i = 0; i < result->n_sort; i++) {
        modbus_point_t *tag =
            *(modbus_point_t **) utarray_front(result->sorts[i].tags);
        struct modbus_sort_ctx *ctx = result->sorts[i].info.context;

        sort_result->cmd[i].tags     = utarray_clone(result->sorts[i].tags);
        sort_result->cmd[i].slave_id = tag->slave_id;
        sort_result->cmd[i].area     = tag->area;
        sort_result->cmd[i].start_address = tag->start_address;
        sort_result->cmd[i].n_register    = ctx->end - ctx->start;

        free(result->sorts[i].info.context);
    }

    neu_tag_sort_free(result);
    return sort_result;
}

int cal_n_byte(int type, neu_value_u *value, neu_datatag_addr_option_u option,
               modbus_endianess endianess, bool default_tag_endian,
               modbus_endianess_64 endianess_64, bool default_tag_endian_64)
{
    int n = 0;
    switch (type) {
    case NEU_TYPE_UINT16:
    case NEU_TYPE_INT16:
        n          = sizeof(uint16_t);
        value->u16 = htons(value->u16);
        break;
    case NEU_TYPE_FLOAT:
    case NEU_TYPE_UINT32:
    case NEU_TYPE_INT32:
        if (default_tag_endian) {
            modbus_convert_endianess(value, endianess);
        }
        n          = sizeof(uint32_t);
        value->u32 = htonl(value->u32);
        break;

    case NEU_TYPE_DOUBLE:
    case NEU_TYPE_INT64:
    case NEU_TYPE_UINT64:
        if (default_tag_endian_64) {
            modbus_convert_endianess_64(value, endianess_64);
        }
        n          = sizeof(uint64_t);
        value->u64 = neu_htonll(value->u64);
        break;
    case NEU_TYPE_BIT: {
        n = sizeof(uint8_t);
        break;
    }
    case NEU_TYPE_STRING: {
        n = option.string.length;
        switch (option.string.type) {
        case NEU_DATATAG_STRING_TYPE_H:
            break;
        case NEU_DATATAG_STRING_TYPE_L:
            neu_datatag_string_ltoh(value->str, option.string.length);
            break;
        case NEU_DATATAG_STRING_TYPE_D:
            break;
        case NEU_DATATAG_STRING_TYPE_E:
            break;
        }
        break;
    }
    case NEU_TYPE_BYTES: {
        n = option.bytes.length;
        break;
    }
    default:
        assert(1 == 0);
        break;
    }
    return n;
}

modbus_write_cmd_sort_t *
modbus_write_tags_sort(UT_array *tags, modbus_endianess endianess,
                       modbus_endianess_64 endianess_64)
{
    neu_tag_sort_result_t *result =
        neu_tag_sort(tags, tag_sort_write, tag_cmp_write);

    modbus_write_cmd_sort_t *sort_result =
        calloc(1, sizeof(modbus_write_cmd_sort_t));
    sort_result->n_cmd = result->n_sort;
    sort_result->cmd   = calloc(result->n_sort, sizeof(modbus_write_cmd_t));
    for (uint16_t i = 0; i < result->n_sort; i++) {
        modbus_point_write_t *tag =
            *(modbus_point_write_t **) utarray_front(result->sorts[i].tags);
        struct modbus_sort_ctx *ctx = result->sorts[i].info.context;

        int num_tags              = utarray_len(result->sorts[i].tags);
        sort_result->cmd[i].bytes = calloc(num_tags, sizeof(neu_value_u));
        int      n_byte = 0, n_byte_tag = 0;
        uint8_t *data_bit = calloc((num_tags + 7) / 8, sizeof(uint8_t));
        int      k        = 0;
        utarray_foreach(result->sorts[i].tags, modbus_point_write_t **, tag_s)
        {
            if ((*tag_s)->point.area == MODBUS_AREA_COIL) {
                n_byte_tag = cal_n_byte((*tag_s)->point.type, &(*tag_s)->value,
                                        (*tag_s)->point.option, endianess, true,
                                        endianess_64, true);
                data_bit[k / 8] += ((*tag_s)->value.i8) << k % 8;
                n_byte += n_byte_tag;
                k++;
            } else {
                n_byte_tag = cal_n_byte(
                    (*tag_s)->point.type, &(*tag_s)->value,
                    (*tag_s)->point.option, endianess,
                    (*tag_s)->point.option.value32.is_default, endianess_64,
                    (*tag_s)->point.option.value64.is_default);
                memcpy(sort_result->cmd[i].bytes +
                           2 *
                               ((*tag_s)->point.start_address -
                                tag->point.start_address),
                       &((*tag_s)->value), n_byte_tag);
                n_byte += n_byte_tag;
            }
        }
        if ((*(modbus_point_write_t **) utarray_front(result->sorts[i].tags))
                ->point.area == MODBUS_AREA_COIL) {
            memcpy(sort_result->cmd[i].bytes, data_bit, (k + 7) / 8);
        }

        sort_result->cmd[i].tags     = utarray_clone(result->sorts[i].tags);
        sort_result->cmd[i].slave_id = tag->point.slave_id;
        sort_result->cmd[i].area     = tag->point.area;
        sort_result->cmd[i].start_address = tag->point.start_address;
        sort_result->cmd[i].n_register    = ctx->end - ctx->start;
        sort_result->cmd[i].n_byte        = n_byte;

        free(data_bit);
        free(result->sorts[i].info.context);
    }

    neu_tag_sort_free(result);
    return sort_result;
}

void modbus_tag_sort_free(modbus_read_cmd_sort_t *cs)
{
    for (uint16_t i = 0; i < cs->n_cmd; i++) {
        utarray_free(cs->cmd[i].tags);
    }

    free(cs->cmd);
    free(cs);
}

static int tag_cmp(neu_tag_sort_elem_t *tag1, neu_tag_sort_elem_t *tag2)
{
    modbus_point_t *p_t1 = (modbus_point_t *) tag1->tag;
    modbus_point_t *p_t2 = (modbus_point_t *) tag2->tag;

    if (p_t1->slave_id > p_t2->slave_id) {
        return 1;
    } else if (p_t1->slave_id < p_t2->slave_id) {
        return -1;
    }

    if (p_t1->area > p_t2->area) {
        return 1;
    } else if (p_t1->area < p_t2->area) {
        return -1;
    }

    if (p_t1->start_address > p_t2->start_address) {
        return 1;
    } else if (p_t1->start_address < p_t2->start_address) {
        return -1;
    }

    if (p_t1->n_register > p_t2->n_register) {
        return 1;
    } else if (p_t1->n_register < p_t2->n_register) {
        return -1;
    }

    return 0;
}

static bool tag_sort(neu_tag_sort_t *sort, void *tag, void *tag_to_be_sorted)
{
    modbus_point_t *        t1  = (modbus_point_t *) tag;
    modbus_point_t *        t2  = (modbus_point_t *) tag_to_be_sorted;
    struct modbus_sort_ctx *ctx = NULL;

    if (sort->info.context == NULL) {
        sort->info.context = calloc(1, sizeof(struct modbus_sort_ctx));
        ctx                = (struct modbus_sort_ctx *) sort->info.context;
        ctx->start         = t1->start_address;
        ctx->end           = t1->start_address + t1->n_register;
        return true;
    }

    ctx = (struct modbus_sort_ctx *) sort->info.context;

    if (t1->slave_id != t2->slave_id) {
        return false;
    }

    if (t1->area != t2->area) {
        return false;
    }

    if (t2->start_address > ctx->end) {
        return false;
    }

    switch (t1->area) {
    case MODBUS_AREA_COIL:
    case MODBUS_AREA_INPUT:
        if ((ctx->end - ctx->start + 7) / 8 >= modbus_read_max_byte) {
            return false;
        }
        break;
    case MODBUS_AREA_INPUT_REGISTER:
    case MODBUS_AREA_HOLD_REGISTER: {
        uint16_t now_bytes = (ctx->end - ctx->start) * 2;
        uint16_t add_now   = now_bytes + t2->n_register * 2;
        if (add_now >= modbus_read_max_byte) {
            return false;
        }

        break;
    }
    }

    if (t2->start_address + t2->n_register > ctx->end) {
        ctx->end = t2->start_address + t2->n_register;
    }

    return true;
}

static int tag_cmp_write(neu_tag_sort_elem_t *tag1, neu_tag_sort_elem_t *tag2)
{
    modbus_point_write_t *p_t1 = (modbus_point_write_t *) tag1->tag;
    modbus_point_write_t *p_t2 = (modbus_point_write_t *) tag2->tag;

    if (p_t1->point.slave_id > p_t2->point.slave_id) {
        return 1;
    } else if (p_t1->point.slave_id < p_t2->point.slave_id) {
        return -1;
    }

    if (p_t1->point.area > p_t2->point.area) {
        return 1;
    } else if (p_t1->point.area < p_t2->point.area) {
        return -1;
    }

    if (p_t1->point.start_address > p_t2->point.start_address) {
        return 1;
    } else if (p_t1->point.start_address < p_t2->point.start_address) {
        return -1;
    }

    if (p_t1->point.n_register > p_t2->point.n_register) {
        return 1;
    } else if (p_t1->point.n_register < p_t2->point.n_register) {
        return -1;
    }

    return 0;
}

static bool tag_sort_write(neu_tag_sort_t *sort, void *tag,
                           void *tag_to_be_sorted)
{
    modbus_point_write_t *  t1  = (modbus_point_write_t *) tag;
    modbus_point_write_t *  t2  = (modbus_point_write_t *) tag_to_be_sorted;
    struct modbus_sort_ctx *ctx = NULL;

    if (sort->info.context == NULL) {
        sort->info.context = calloc(1, sizeof(struct modbus_sort_ctx));
        ctx                = (struct modbus_sort_ctx *) sort->info.context;
        ctx->start         = t1->point.start_address;
        ctx->end           = t1->point.start_address + t1->point.n_register;
        return true;
    }

    ctx = (struct modbus_sort_ctx *) sort->info.context;

    if (t1->point.slave_id != t2->point.slave_id) {
        return false;
    }

    if (t1->point.area != t2->point.area) {
        return false;
    }

    if (t2->point.start_address > ctx->end) {
        return false;
    }

    switch (t1->point.area) {
    case MODBUS_AREA_COIL:
    case MODBUS_AREA_INPUT:
        if ((ctx->end - ctx->start) / 8 >= modbus_read_max_byte - 1) {
            return false;
        }
        break;
    case MODBUS_AREA_INPUT_REGISTER:
    case MODBUS_AREA_HOLD_REGISTER: {
        uint16_t now_bytes = (ctx->end - ctx->start) * 2;
        uint16_t add_now   = now_bytes + t2->point.n_register * 2;
        if (add_now >= modbus_read_max_byte) {
            return false;
        }

        break;
    }
    }

    if (t2->point.start_address + t2->point.n_register > ctx->end) {
        ctx->end = t2->point.start_address + t2->point.n_register;
    }

    return true;
}

void modbus_convert_endianess(neu_value_u *value, modbus_endianess endianess)
{
    switch (endianess) {
    case MODBUS_ABCD:
        break;
    case MODBUS_CDAB:
        value->u32 = htonl(value->u32);
        neu_ntohs_p((uint16_t *) value->bytes.bytes);
        neu_ntohs_p((uint16_t *) (value->bytes.bytes + 2));
        break;
    case MODBUS_BADC:
        neu_ntohs_p((uint16_t *) value->bytes.bytes);
        neu_ntohs_p((uint16_t *) (value->bytes.bytes + 2));
        break;
    case MODBUS_DCBA:
        value->u32 = htonl(value->u32);
        break;
    default:
        break;
    }
}

void modbus_convert_endianess_64(neu_value_u *       value,
                                 modbus_endianess_64 endianess_64)
{
    switch (endianess_64) {
    case MODBUS_LL:
        break;
    case MODBUS_BB:
        value->u64 = neu_htonll(value->u64);
        break;
    case MODBUS_LB:
        value->u64 = neu_htonlb(value->u64);
        break;
    case MODBUS_BL:
        value->u64 = neu_htonbl(value->u64);
        break;
    default:
        break;
    }
}