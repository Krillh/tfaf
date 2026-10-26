#include "tfaf1.h"

#define TFAF_MAGIC 0x2cb8858eu
#define TFAF_FORMAT 0xfaf01000u
#define TFAF_COOKIE 0x54464146u
#define TFAF_DEFAULT_WORK 1000000u

static int alias(const void *a, size_t an, const void *b, size_t bn);

static void bytes_zero(void *p, size_t n) {
  uint8_t *b = (uint8_t *)p;
  size_t i;
  for (i = 0; i < n; ++i)
    b[i] = 0;
}
static uint16_t get16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t get32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
static int add_u64(uint64_t a, uint64_t b, uint64_t *out) {
  if (UINT64_MAX - a < b)
    return 0;
  *out = a + b;
  return 1;
}
static int mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
  if (a && b > UINT64_MAX / a)
    return 0;
  *out = a * b;
  return 1;
}
static void set_owner(uint8_t *p, const void *owner, uint32_t generation) {
  uintptr_t x = (uintptr_t)owner;
  size_t i;
  for (i = 0; i < sizeof(void *); ++i)
    p[i] = (uint8_t)(x >> (i * 8));
  for (i = 0; i < sizeof(uint32_t); ++i)
    p[sizeof(void *) + i] = (uint8_t)(generation >> (i * 8));
}
static int owns(const uint8_t *p, const void *owner, uint32_t generation) {
  uintptr_t x = (uintptr_t)owner;
  size_t i;
  for (i = 0; i < sizeof(void *); ++i)
    if (p[i] != (uint8_t)(x >> (i * 8)))
      return 0;
  for (i = 0; i < sizeof(uint32_t); ++i)
    if (p[sizeof(void *) + i] != (uint8_t)(generation >> (i * 8)))
      return 0;
  return 1;
}
static tfaf_status spend(tfaf_volume *v, uint64_t amount) {
  if (v->work > v->work_limit || amount > v->work_limit - v->work)
    return TFAF_ERESOURCE;
  v->work += amount;
  return TFAF_OK;
}
size_t tfaf1_workspace_size(uint32_t block_size) { return (size_t)block_size; }
void tfaf1_init(tfaf_volume *v) {
  if (!v)
    return;
  bytes_zero(v, sizeof(*v));
  v->_cookie = TFAF_COOKIE;
  v->_generation = 1;
}

static tfaf_status read_block(tfaf_volume *v, uint32_t index) {
  int32_t raw = 0;
  if (index >= v->reader.block_count)
    return TFAF_ECORRUPT;
  if (v->cache_valid && v->cached_block == index)
    return TFAF_OK;
  if (v->reader.read(v->reader.context, index, v->cache, v->cache_size, &raw)) {
    v->raw_io_status = raw;
    v->cache_valid = 0;
    return TFAF_EIO;
  }
  v->cached_block = index;
  v->cache_valid = 1;
  return TFAF_OK;
}
static tfaf_status read_bytes(tfaf_volume *v, uint64_t offset, uint8_t *dst,
                              size_t length) {
  uint64_t disk_bytes, at = offset;
  size_t done = 0;
  tfaf_status s;
  if (!mul_u64(v->reader.block_size, v->reader.block_count, &disk_bytes) ||
      offset > disk_bytes || (uint64_t)length > disk_bytes - offset)
    return TFAF_ECORRUPT;
  while (done < length) {
    uint32_t block = (uint32_t)(at / v->reader.block_size);
    size_t in_block = (size_t)(at % v->reader.block_size);
    size_t take = v->reader.block_size - in_block;
    if (take > length - done)
      take = length - done;
    s = read_block(v, block);
    if (s != TFAF_OK)
      return s;
    for (size_t i = 0; i < take; ++i)
      dst[done + i] = v->cache[in_block + i];
    done += take;
    at += take;
  }
  return TFAF_OK;
}
static tfaf_status parse_entry(tfaf_volume *v, uint32_t index,
                               uint64_t offset, tfaf_file *f) {
  uint8_t fields[8];
  uint64_t name_at, end;
  tfaf_status s;
  if (index >= v->entry_count)
    return TFAF_EEND;
  if (offset > v->table_end || sizeof(fields) > v->table_end - offset)
    return TFAF_ECORRUPT;
  if ((s = read_bytes(v, offset, fields, sizeof(fields))) != TFAF_OK)
    return s;
  f->index = index;
  f->size = get32(fields);
  f->first_block = get16(fields + 4);
  f->name_length = get16(fields + 6);
  f->_table_offset = offset;
  if (!f->name_length)
    return TFAF_ECORRUPT;
  if (!add_u64(offset, sizeof(fields), &name_at) ||
      !add_u64(name_at, f->name_length, &end) || end > v->table_end)
    return TFAF_ECORRUPT;
  set_owner(f->_owner, v, v->_generation);
  return TFAF_OK;
}
static tfaf_status entry_offset(tfaf_volume *v, uint32_t wanted,
                                uint64_t *offset) {
  uint64_t at = 16;
  tfaf_status s;
  for (uint32_t i = 0; i < wanted; ++i) {
    uint8_t fields[8];
    uint64_t next;
    uint16_t n;
    if (!add_u64(at, sizeof(fields), &next) || next > v->table_end)
      return TFAF_ECORRUPT;
    s = read_bytes(v, at, fields, sizeof(fields));
    if (s != TFAF_OK)
      return s;
    n = get16(fields + 6);
    if (!n || !add_u64(next, n, &at) || at > v->table_end)
      return TFAF_ECORRUPT;
  }
  *offset = at;
  return TFAF_OK;
}
static tfaf_status get_entry(tfaf_volume *v, uint32_t index, tfaf_file *f) {
  uint64_t offset;
  tfaf_status s;
  if (!v || !f || !v->active)
    return TFAF_EINVAL;
  if (alias(f, sizeof(*f), v, sizeof(*v)) ||
      alias(f, sizeof(*f), v->cache, v->cache_size))
    return TFAF_EINVAL;
  if (index >= v->entry_count)
    return TFAF_EEND;
  s = entry_offset(v, index, &offset);
  if (s != TFAF_OK)
    return s;
  return parse_entry(v, index, offset, f);
}
tfaf_status tfaf1_entry_at(tfaf_volume *v, uint32_t index, tfaf_file *f) {
  return get_entry(v, index, f);
}
tfaf_status tfaf1_entries_begin(tfaf_volume *v, tfaf_cursor *cursor) {
  if (!v || !cursor || !v->active)
    return TFAF_EINVAL;
  if (alias(cursor, sizeof(*cursor), v, sizeof(*v)) ||
      alias(cursor, sizeof(*cursor), v->cache, v->cache_size))
    return TFAF_EINVAL;
  cursor->next_index = 0;
  cursor->_table_offset = 16;
  set_owner(cursor->_owner, v, v->_generation);
  return TFAF_OK;
}
tfaf_status tfaf1_entries_next(tfaf_volume *v, tfaf_cursor *cursor,
                                tfaf_file *file) {
  tfaf_status s;
  if (!v || !cursor || !file || !v->active ||
      !owns(cursor->_owner, v, v->_generation))
    return TFAF_EINVAL;
  if (alias(cursor, sizeof(*cursor), v, sizeof(*v)) ||
      alias(cursor, sizeof(*cursor), v->cache, v->cache_size) ||
      alias(file, sizeof(*file), v, sizeof(*v)) ||
      alias(file, sizeof(*file), v->cache, v->cache_size) ||
      alias(file, sizeof(*file), cursor, sizeof(*cursor)))
    return TFAF_EINVAL;
  if (cursor->next_index >= v->entry_count)
    return TFAF_EEND;
  s = parse_entry(v, cursor->next_index, cursor->_table_offset, file);
  if (s != TFAF_OK)
    return s;
  if (!add_u64(cursor->_table_offset, 8u + file->name_length,
               &cursor->_table_offset))
    return TFAF_ECORRUPT;
  cursor->next_index++;
  return TFAF_OK;
}
static int ranges_overlap(uint32_t a, uint32_t ac, uint32_t b, uint32_t bc) {
  return (uint64_t)a < (uint64_t)b + bc && (uint64_t)b < (uint64_t)a + ac;
}
static tfaf_status read_name(tfaf_volume *v, const tfaf_file *f, uint8_t *dst) {
  return read_bytes(v, f->_table_offset + 8, dst, f->name_length);
}
static tfaf_status validate_impl(tfaf_volume *v) {
  uint64_t at = 16;
  tfaf_status s;
  for (uint32_t i = 0; i < v->entry_count; ++i) {
    uint8_t fields[8];
    uint16_t name_length;
    uint32_t size, first, blocks;
    uint64_t name_at, next;
    if ((s = spend(v, 1)) != TFAF_OK)
      return s;
    if (!add_u64(at, 8, &name_at) || name_at > v->table_end)
      return TFAF_ECORRUPT;
    s = read_bytes(v, at, fields, sizeof(fields));
    if (s != TFAF_OK)
      return s;
    size = get32(fields);
    first = get16(fields + 4);
    name_length = get16(fields + 6);
    if (!name_length || !add_u64(name_at, name_length, &next) ||
        next > v->table_end)
      return TFAF_ECORRUPT;
    if (first < v->contents_block || first > v->reader.block_count)
      return TFAF_ECORRUPT;
    blocks = size / v->reader.block_size +
             (size % v->reader.block_size != 0);
    if (blocks > v->reader.block_count - first)
      return TFAF_ECORRUPT;

    /* Names must be printable ASCII and are compared as counted byte strings. */
    for (uint32_t n = 0; n < name_length; ++n) {
      uint8_t c;
      if ((s = spend(v, 1)) != TFAF_OK)
        return s;
      if ((s = read_bytes(v, name_at + n, &c, 1)) != TFAF_OK)
        return s;
      if (c < 0x20 || c > 0x7e)
        return TFAF_ECORRUPT;
    }

    /* Check exact-name uniqueness and disjoint block ownership against prior entries. */
    uint64_t prior_at = 16;
    for (uint32_t j = 0; j < i; ++j) {
      uint8_t prior_fields[8];
      uint64_t prior_name, prior_end;
      uint16_t prior_length;
      uint32_t prior_size, prior_first, prior_blocks;
      if ((s = spend(v, 1)) != TFAF_OK)
        return s;
      if (!add_u64(prior_at, 8, &prior_name) || prior_name > v->table_end)
        return TFAF_ECORRUPT;
      s = read_bytes(v, prior_at, prior_fields, 8);
      if (s != TFAF_OK)
        return s;
      prior_size = get32(prior_fields);
      prior_first = get16(prior_fields + 4);
      prior_length = get16(prior_fields + 6);
      if (!prior_length || !add_u64(prior_name, prior_length, &prior_end) ||
          prior_end > v->table_end)
        return TFAF_ECORRUPT;
      if (prior_length == name_length) {
        for (uint32_t n = 0; n < name_length; ++n) {
          uint8_t a, b;
          if ((s = spend(v, 1)) != TFAF_OK)
            return s;
          if ((s = read_bytes(v, prior_name + n, &a, 1)) != TFAF_OK ||
              (s = read_bytes(v, name_at + n, &b, 1)) != TFAF_OK)
            return s;
          if (a != b)
            break;
          if (n + 1 == name_length)
            return TFAF_ECORRUPT;
        }
      }
      prior_blocks = prior_size / v->reader.block_size +
                     (prior_size % v->reader.block_size != 0);
      if (blocks && prior_blocks &&
          ranges_overlap(first, blocks, prior_first, prior_blocks))
        return TFAF_ECORRUPT;
      prior_at = prior_end;
    }
    at = next;
  }
  if (at != v->table_end)
    return TFAF_ECORRUPT;
  return TFAF_OK;
}
tfaf_status tfaf1_validate(tfaf_volume *v, uint64_t limit) {
  if (!v || !v->active)
    return TFAF_EINVAL;
  v->work = 0;
  v->work_limit = limit ? limit : TFAF_DEFAULT_WORK;
  return validate_impl(v);
}
tfaf_status tfaf1_mount(tfaf_volume *v, const tfaf_reader *r, void *workspace,
                          size_t workspace_size, uint64_t work_limit) {
  uint8_t header[16];
  uint64_t disk_bytes, table_start, content_start;
  tfaf_status s;
  uint32_t generation;
  if (!v || v->_cookie != TFAF_COOKIE)
    return TFAF_EINVAL;
  if ((r && alias(r, sizeof(*r), v, sizeof(*v))) ||
      (workspace && (alias(workspace, workspace_size, v, sizeof(*v)) ||
                    alias(workspace, workspace_size, r, r ? sizeof(*r) : 0))))
    return TFAF_EINVAL;
  generation = v->_generation + 1;
  if (!generation)
    generation = 1;
  bytes_zero(v, sizeof(*v));
  v->_cookie = TFAF_COOKIE;
  v->_generation = generation;
  if (!r || !r->read || !workspace)
    return TFAF_EINVAL;
  if (r->block_size < 16 || r->block_size > 65535 || !r->block_count ||
      r->block_count > 65535)
    return TFAF_ECORRUPT;
  if (workspace_size < r->block_size)
    return TFAF_ENOSPACE;
  v->reader = *r;
  v->cache = (uint8_t *)workspace;
  v->cache_size = r->block_size;
  v->work_limit = work_limit ? work_limit : TFAF_DEFAULT_WORK;
  if (!mul_u64(r->block_size, r->block_count, &disk_bytes)) {
    s = TFAF_ECORRUPT;
    goto fail;
  }
  s = read_bytes(v, 0, header, sizeof(header));
  if (s != TFAF_OK)
    goto fail;
  if (get32(header) != TFAF_MAGIC || get32(header + 4) != TFAF_FORMAT) {
    s = TFAF_EFORMAT;
    goto fail;
  }
  if (get16(header + 8) != r->block_size ||
      get16(header + 10) != r->block_count) {
    s = TFAF_ECORRUPT;
    goto fail;
  }
  if (r->block_size < 16) {
    s = TFAF_ECORRUPT;
    goto fail;
  }
  v->entry_count = get16(header + 12);
  v->contents_block = get16(header + 14);
  if (v->contents_block > r->block_count ||
      !mul_u64(v->contents_block, r->block_size, &content_start) ||
      content_start < 16 || content_start > disk_bytes) {
    s = TFAF_ECORRUPT;
    goto fail;
  }
  table_start = 16;
  v->table_end = table_start;
  v->active = 1;
  /* Parse exact entry count to discover the unaligned, possibly multi-block end. */
  for (uint32_t i = 0; i < v->entry_count; ++i) {
    uint8_t fields[8];
    uint64_t name_at, next;
    if (!add_u64(v->table_end, 8, &name_at) || name_at > content_start) {
      s = TFAF_ECORRUPT;
      goto fail;
    }
    s = read_bytes(v, v->table_end, fields, sizeof(fields));
    if (s != TFAF_OK)
      goto fail;
    uint16_t n = get16(fields + 6);
    if (!n || !add_u64(name_at, n, &next) || next > content_start) {
      s = TFAF_ECORRUPT;
      goto fail;
    }
    v->table_end = next;
  }
  v->work = 0;
  s = validate_impl(v);
  if (s != TFAF_OK)
    goto fail;
  return TFAF_OK;
fail: {
  int32_t raw = v->raw_io_status;
  bytes_zero(v, sizeof(*v));
  v->_cookie = TFAF_COOKIE;
  v->_generation = generation;
  v->raw_io_status = raw;
  return s;
}
}
void tfaf1_unmount(tfaf_volume *v) {
  if (!v)
    return;
  uint32_t generation = v->_cookie == TFAF_COOKIE ? v->_generation + 1 : 1;
  if (!generation)
    generation = 1;
  bytes_zero(v, sizeof(*v));
  v->_cookie = TFAF_COOKIE;
  v->_generation = generation;
}

static int alias(const void *a, size_t an, const void *b, size_t bn) {
  uintptr_t as = (uintptr_t)a, bs = (uintptr_t)b, ae, be;
  if (!an || !bn)
    return 0;
  if (an > UINTPTR_MAX - as || bn > UINTPTR_MAX - bs)
    return 1;
  ae = as + an;
  be = bs + bn;
  return as < be && bs < ae;
}
static tfaf_status canonical(tfaf_volume *v, const tfaf_file *f,
                             tfaf_file *out) {
  tfaf_status s;
  if (!v || !f || !out || !v->active ||
      !owns(f->_owner, v, v->_generation))
    return TFAF_EINVAL;
  s = get_entry(v, f->index, out);
  if (s != TFAF_OK)
    return s;
  if (out->size != f->size || out->first_block != f->first_block ||
      out->name_length != f->name_length ||
      out->_table_offset != f->_table_offset)
    return TFAF_EINVAL;
  return TFAF_OK;
}
tfaf_status tfaf1_file_name(tfaf_volume *v, const tfaf_file *f,
                             uint8_t *dst, size_t capacity,
                             size_t *name_length) {
  tfaf_file actual;
  tfaf_status s;
  if (!v || !f || !dst || !name_length)
    return TFAF_EINVAL;
  if (alias(name_length, sizeof(*name_length), v, sizeof(*v)) ||
      alias(name_length, sizeof(*name_length), v->cache, v->cache_size) ||
      alias(name_length, sizeof(*name_length), f, sizeof(*f)) ||
      alias(name_length, sizeof(*name_length), dst, capacity))
    return TFAF_EINVAL;
  *name_length = 0;
  if ((s = canonical(v, f, &actual)) != TFAF_OK)
    return s;
  if (alias(dst, capacity, v->cache, v->cache_size) ||
      alias(dst, capacity, v, sizeof(*v)) ||
      alias(dst, capacity, f, sizeof(*f)))
    return TFAF_EINVAL;
  if ((size_t)actual.name_length + 1u > capacity)
    return TFAF_ENOSPACE;
  s = read_name(v, &actual, dst);
  if (s == TFAF_OK) {
    dst[actual.name_length] = 0;
    *name_length = actual.name_length;
  }
  return s;
}
tfaf_status tfaf1_lookup(tfaf_volume *v, const uint8_t *name, size_t length,
                          tfaf_file *out) {
  uint64_t at = 16;
  tfaf_status s;
  if (!v || !out || (!name && length) || !length || !v->active)
    return TFAF_EINVAL;
  if (alias(out, sizeof(*out), v, sizeof(*v)) ||
      alias(out, sizeof(*out), v->cache, v->cache_size))
    return TFAF_EINVAL;
  if (length > UINT16_MAX)
    return TFAF_ENOTFOUND;
  if (alias(name, length, v->cache, v->cache_size) ||
      alias(name, length, v, sizeof(*v)))
    return TFAF_EINVAL;
  for (size_t n = 0; n < length; ++n)
    if (name[n] < 0x20 || name[n] > 0x7e)
      return TFAF_EINVAL;
  for (uint32_t i = 0; i < v->entry_count; ++i) {
    uint8_t fields[8];
    uint64_t name_at;
    uint16_t n;
    if (!add_u64(at, 8, &name_at) || name_at > v->table_end)
      return TFAF_ECORRUPT;
    s = read_bytes(v, at, fields, 8);
    if (s != TFAF_OK)
      return s;
    n = get16(fields + 6);
    if (!n || !add_u64(name_at, n, &at) || at > v->table_end)
      return TFAF_ECORRUPT;
    if (n == length) {
      int equal = 1;
      for (size_t k = 0; k < length; ++k) {
        uint8_t c;
        if ((s = read_bytes(v, name_at + k, &c, 1)) != TFAF_OK)
          return s;
        if (c != name[k]) {
          equal = 0;
          break;
        }
      }
      if (equal)
        return parse_entry(v, i, at - n - 8, out);
    }
  }
  return TFAF_ENOTFOUND;
}
tfaf_status tfaf1_pread(tfaf_volume *v, const tfaf_file *f, uint64_t offset,
                         void *destination, size_t length, size_t *bytes_read) {
  tfaf_file actual;
  uint64_t pos;
  size_t done = 0;
  tfaf_status s;
  if (!v || !f || (!destination && length) || !bytes_read)
    return TFAF_EINVAL;
  if (alias(bytes_read, sizeof(*bytes_read), v, sizeof(*v)) ||
      alias(bytes_read, sizeof(*bytes_read), v->cache, v->cache_size) ||
      alias(bytes_read, sizeof(*bytes_read), f, sizeof(*f)) ||
      alias(bytes_read, sizeof(*bytes_read), destination, length))
    return TFAF_EINVAL;
  *bytes_read = 0;
  if (alias(f, sizeof(*f), v, sizeof(*v)))
    return TFAF_EINVAL;
  if ((s = canonical(v, f, &actual)) != TFAF_OK)
    return s;
  if (alias(destination, length, v->cache, v->cache_size) ||
      alias(destination, length, v, sizeof(*v)) ||
      alias(destination, length, f, sizeof(*f)))
    return TFAF_EINVAL;
  if (offset >= actual.size || !length)
    return TFAF_OK;
  if ((uint64_t)length > actual.size - offset)
    length = (size_t)(actual.size - offset);
  if (!mul_u64(actual.first_block, v->reader.block_size, &pos) ||
      !add_u64(pos, offset, &pos))
    return TFAF_ECORRUPT;
  while (done < length) {
    uint32_t block = (uint32_t)(pos / v->reader.block_size);
    size_t in_block = (size_t)(pos % v->reader.block_size);
    size_t take = v->reader.block_size - in_block;
    if (take > length - done)
      take = length - done;
    s = read_block(v, block);
    if (s != TFAF_OK) {
      *bytes_read = done;
      return s;
    }
    for (size_t i = 0; i < take; ++i)
      ((uint8_t *)destination)[done + i] = v->cache[in_block + i];
    done += take;
    pos += take;
  }
  *bytes_read = done;
  return TFAF_OK;
}














