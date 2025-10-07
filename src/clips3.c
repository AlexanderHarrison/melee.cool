// TODO: comprehensive testing

typedef U32 HashKey;
typedef U32 MetaIdx;

typedef struct TagEntry {
    HashKey hash;
    U32 table_size;
} TagEntry;

static HashKey hash_bytes(const U8* key, U64 len);

typedef struct AutoCompleteHashEntry {
    HashKey hash;
    U32 list_idx;
} AutoCompleteHashEntry;

typedef struct AutoCompleteLookup {
    U32 count;
    U32 tag_table_sizes[10];
    char *tag_names[10];
} AutoCompleteLookup;

/*
pseudo-struct:
struct AutocompleteList {
    U32 count;
    u32 tag_table_sizes[count];
    char null_terminated_tag_name_postfixes * count;
}
*/
char *autocomplete_lists;
AutoCompleteHashEntry *autocomplete_table;
U32 autocomplete_table_cap;

#define META_MAX 2048

static Str all = ConstStr("!all");

// lookup that the tag exists and the number of entries it has 
static TagEntry *tag_table;
static U32 tag_table_size;
static U32 tag_table_cap;

AutoCompleteLookup tag_autocomplete(Str tag) {
    AutoCompleteLookup ret = { 0 };

    AutoCompleteHashEntry *entry; 
    HashKey key = hash_bytes((U8 *)tag.buf, tag.len);
    HashKey idx = key & (autocomplete_table_cap-1);
    while (1) {
        entry = &autocomplete_table[idx];
        if (entry->hash == key) {
            break;
        } else if (entry->hash == 0) {
            // not found
            return ret;
        }
        idx = (idx + 1) & (autocomplete_table_cap-1);
    }
    
    U32 list_idx = entry->list_idx;
    U32 *list = (U32*)&autocomplete_lists[list_idx];
    
    ret.count = *list;
    
    U32 *table_sizes = list + 1;
    for (U32 i = 0; i < ret.count; ++i)
        ret.tag_table_sizes[i] = table_sizes[i];
    
    char *post_fixes = (char*)(table_sizes + ret.count);
    for (U32 i = 0; i < ret.count; ++i) {
        ret.tag_names[i] = post_fixes;
    
        // skip to next tag name
        while (*(post_fixes++));
    }
    
    return ret;
}

static int filter_file(const struct dirent *entry) {
    return entry->d_type == DT_REG;
}

#define TAG_MAX 32
#define TAG_LEN_MAX 63
typedef struct EntrySearch {
    U32 tag_count;
    TagEntry tag_entry[TAG_MAX];
    Str tag_names[TAG_MAX];
    U32 tag_search_idx[TAG_MAX];
} EntrySearch;

static HashKey murmur_32_scramble(U32 k) {
    k *= 0xcc9e2d51;
    k = (k << 15) | (k >> 17);
    k *= 0x1b873593;
    return k;
}

static HashKey hash_bytes(const U8* key, U64 len) {
    // murmur 3
    U32 h = 0x1b873593;
    U32 k;
    for (U64 i = len >> 2; i; i--) {
        memcpy(&k, key, sizeof(U32));
        key += sizeof(U32);
        h ^= murmur_32_scramble(k);
        h = (h << 13) | (h >> 19);
        h = h * 5 + 0xe6546b64;
    }
    k = 0;
    for (U64 i = len & 3; i; i--) {
        k <<= 8;
        k |= key[i - 1];
    }
    h ^= murmur_32_scramble(k);
    h ^= (U32)len;
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

static const char clipsdir[] = "./clips/";
static const char tagdir[] = "./clips/clip_tags/";
static const char metapath[] = "./clips/metadata";
static int meta_fd;
static int tagdir_fd;

// valid until next call to read_metadata
// returns false on error
static bool read_metadata(char *buf, MetaIdx idx) {
    if (lseek(meta_fd, idx, SEEK_SET) != idx)
        return false;
    
    ssize_t size = read(meta_fd, buf, META_MAX);
    if (size < 0)
        return false;
    
    return true;
}

static MetaIdx *tag_entry_table_get(TagEntry *tag_entry, Str tag_name) {
    if (tag_entry->table_size == 0)
        return NULL;

    char temp_pathname[TAG_LEN_MAX+1];
    memcpy(temp_pathname, tag_name.buf, tag_name.len);
    temp_pathname[tag_name.len] = 0;
    
    int fd = openat(tagdir_fd, temp_pathname, O_RDONLY, S_IRUSR | S_IWUSR);
    if (fd < 0)
        return NULL;
    
    MetaIdx *table = mmap(NULL, tag_entry->table_size, PROT_READ, MAP_PRIVATE, fd, 0); 
    if (table == MAP_FAILED) {
        close(fd);
        return NULL;
    }
    
    if (close(fd) != 0) {
        munmap(table, tag_entry->table_size);
        return NULL;
    }
    
    return table;
}

static void tag_entry_table_close(MetaIdx *table, TagEntry *tag_entry) {
    munmap(table, tag_entry->table_size);
}

static void realloc_tag_table(void) {
    printf("realloc\n");
    U32 new_tag_table_cap = tag_table_cap * 2;
    TagEntry *new_table = reallocarray(tag_table, new_tag_table_cap, sizeof(*tag_table));
    
    for (USize i = 0; i < tag_table_cap; ++i) {
        TagEntry *old_entry = &tag_table[i];
        if (old_entry->hash == 0)
            continue;
            
        HashKey idx = old_entry->hash & (new_tag_table_cap-1);
        while (1) {
            TagEntry *new_entry = &new_table[idx];
            if (new_entry->hash == 0) {
                *new_entry = *old_entry;
                break;
            }
            idx = (idx + 1) & (new_tag_table_cap-1);
        }
    }
    
    tag_table = new_table;
    tag_table_cap = new_tag_table_cap;
}

static TagEntry *lookup_tag(Str tag) {
    // hash tag
    HashKey key = hash_bytes((const U8*)tag.buf, tag.len);
    
    // lookup hash
    HashKey init_idx = key & (tag_table_cap-1);
    HashKey idx = init_idx;

    while (1) {
        TagEntry *entry = &tag_table[idx];
        if (entry->hash == key) {
            return entry;
        } else if (entry->hash == 0) {
            // create new tag table
            entry->hash = key;
            tag_table_size++;
            
            if (tag_table_size * 2 < tag_table_cap) {
                return entry;
            } else {
                realloc_tag_table();
                idx = init_idx;
                continue;
            }
        }
        idx = (idx + 1) & (tag_table_cap-1);
    }
}

static void tag_entry(MetaIdx meta_idx, Str tag) {
    TagEntry *tag_entry = lookup_tag(tag);

    char temp_pathname[TAG_LEN_MAX+1];
    memcpy(temp_pathname, tag.buf, tag.len);
    temp_pathname[tag.len] = 0;
    
    int fd = openat(tagdir_fd, temp_pathname, O_WRONLY|O_APPEND|O_CREAT, S_IRUSR|S_IWUSR);
    if (fd < 0) {
        while (1);
        // IDK WHAT TO DO HERE TODO TODO TODO TODO --------------------------------------
    }

    ssize_t ret = write(fd, (char*)&meta_idx, sizeof(meta_idx));
    if (ret != sizeof(U32)) {
        while (1);
        // IDK WHAT TO DO HERE TODO TODO TODO TODO --------------------------------------
    }
    
    tag_entry->table_size++;
    
    if (close(fd) != 0) {
        while (1);
        // IDK WHAT TO DO HERE TODO TODO TODO TODO --------------------------------------
    }
}

static MetaIdx create_entry(Str new_meta) {
    off_t seek_ret = lseek(meta_fd, 0, SEEK_END);
    if (seek_ret < 0) {
        while (1);
        // IDK WHAT TO DO HERE TODO TODO TODO TODO --------------------------------------
    }
    MetaIdx meta_idx = (MetaIdx)seek_ret;
    
    ssize_t ret = write(meta_fd, new_meta.buf, new_meta.len);
    if (ret != (ssize_t)new_meta.len) {
        while (1);
        // IDK WHAT TO DO HERE TODO TODO TODO TODO --------------------------------------
    }
    
    tag_entry(meta_idx, all);
    
    return meta_idx;
}

static void search_tag(EntrySearch *search, Str tag) {
    // insert into tag list, sorted by increasing entry count
    
    TagEntry *tag_entry = lookup_tag(tag);
    U32 table_size = tag_entry->table_size;
    
    U32 i = search->tag_count;
    while (
        i != 0
        && search->tag_entry[i-1].table_size > table_size
    ) {
        search->tag_entry[i] = search->tag_entry[i-1];
        search->tag_names[i] = search->tag_names[i-1];
        i -= 1;
    }
    search->tag_entry[i] = *tag_entry;
    search->tag_names[i] = tag;
    search->tag_count++;
}

// TODO single tag optimization?

static bool search_next_tagless(char *buf, EntrySearch *search) {
    // sort by newest using the all tag
    
    TagEntry *tag_entry = lookup_tag(all);
    MetaIdx search_i = search->tag_search_idx[0];
    if (search_i >= tag_entry->table_size)
        return false;

    MetaIdx *tag_entry_table = tag_entry_table_get(tag_entry, all);
    if (tag_entry_table == NULL)
        return false;
    
    search->tag_search_idx[0]++;
    U32 i = tag_entry->table_size - search_i - 1;
    MetaIdx meta_i = tag_entry_table[i];
    tag_entry_table_close(tag_entry_table, tag_entry);

    return read_metadata(buf, meta_i);
}

static bool search_prev_tagless(char *buf, EntrySearch *search) {
    // sort by newest using the all tag
    
    MetaIdx search_i = search->tag_search_idx[0];
    if (search_i == 0)
        return false;

    TagEntry *tag_entry = lookup_tag(all);
    MetaIdx *tag_entry_table = tag_entry_table_get(tag_entry, all);
    if (tag_entry_table == NULL)
        return false;
    
    search_i--;
    search->tag_search_idx[0] = search_i;
    U32 i = tag_entry->table_size - search_i - 1;
    MetaIdx meta_i = tag_entry_table[i];
    tag_entry_table_close(tag_entry_table, tag_entry);
    
    return read_metadata(buf, meta_i);
}

static bool search_next(char *buf, EntrySearch *search) {
    if (search->tag_count == 0)
        return search_next_tagless(buf, search);
    
    if (search->tag_entry[0].table_size == 0)
        return false;
    
    MetaIdx *tag_entry_tables[TAG_MAX];
    for (U32 i = 0; i < search->tag_count; ++i) {
        tag_entry_tables[i] = tag_entry_table_get(
            &search->tag_entry[i],
            search->tag_names[i]
        );
        
        if (tag_entry_tables[i] == NULL) {
            for (; i != 0; --i)
                tag_entry_table_close(tag_entry_tables[i-1], &search->tag_entry[i-1]);
            return false;
        }
    }
    
    // TODO - when should we binary search instead of linear scan?
    MetaIdx idx_min = 0xFFFFFFFF;
    
    bool ret;
    U32 tag_i = 0;
    U32 tag_count = search->tag_count;
    while (1) {
        TagEntry *tag_entry = &search->tag_entry[tag_i];
        U32 tag_entry_count = tag_entry->table_size;
        U32 tag_search_idx = search->tag_search_idx[tag_i];
        if (tag_search_idx >= tag_entry_count) {
            ret = false;
            break;
        }
        U32 tag_entry_idx = tag_entry_count - tag_search_idx - 1;
        
        MetaIdx prev_idx = tag_entry_tables[tag_i][tag_entry_idx];
        
        if (prev_idx > idx_min) {
            // try next entry for this tag
            search->tag_search_idx[tag_i] += 1;
        } else if (prev_idx < idx_min) {
            // idx_min entry not found for this tag, start again 
            idx_min = prev_idx;
            tag_i = 0;
        } else {
            // idx_min entry found for this tag, check next tag 
            search->tag_search_idx[tag_i] += 1;
            tag_i += 1;
            
            // return if all tags found for this entry
            if (tag_i == tag_count) {
                ret = read_metadata(buf, idx_min);
                break;
            }
        }
    }
    
    for (U32 i = 0; i < search->tag_count; ++i)
        tag_entry_table_close(tag_entry_tables[i], &search->tag_entry[i]);
        
    return ret;
}

static bool search_prev(char *buf, EntrySearch *search) {
    if (search->tag_count == 0)
        return search_prev_tagless(buf, search);

    if (search->tag_entry[0].table_size == 0)
        return false;
    
    MetaIdx *tag_entry_tables[TAG_MAX];
    for (U32 i = 0; i < search->tag_count; ++i) {
        tag_entry_tables[i] = tag_entry_table_get(
            &search->tag_entry[i],
            search->tag_names[i]
        );
        
        if (tag_entry_tables[i] == NULL) {
            for (; i != 0; --i)
                tag_entry_table_close(tag_entry_tables[i-1], &search->tag_entry[i-1]);
            return false;
        }
    }
    
    // TODO - when should we binary search instead of linear scan?
    MetaIdx idx_max = 0;
    U32 tag_count = search->tag_count;
    
    // clamp
    for (USize i = 0; i < tag_count; ++i) {
        TagEntry *tag_entry = &search->tag_entry[i];
        U32 tag_entry_count = tag_entry->table_size;
        if (tag_entry_count == 0)
            return false;

        U32 tag_search_idx = search->tag_search_idx[i];
        if (tag_search_idx >= tag_entry_count)
            search->tag_search_idx[i] = tag_entry_count;
    }
    
    USize tag_i = 0;
    bool ret = true;
    while (1) {
        TagEntry *tag_entry = &search->tag_entry[tag_i];
        U32 tag_entry_count = tag_entry->table_size;
        U32 tag_search_idx = search->tag_search_idx[tag_i];
        
        if (tag_search_idx == 0) {
            ret = false;
            break;
        }
        --tag_search_idx;

        U32 tag_entry_idx = tag_entry_count - tag_search_idx - 1;
        MetaIdx next_idx = tag_entry_tables[tag_i][tag_entry_idx];
        
        if (next_idx < idx_max) {
            // try next entry for this tag
            search->tag_search_idx[tag_i] -= 1;
        } else if (next_idx > idx_max) {
            // idx_max entry not found for this tag, start again 
            idx_max = next_idx;
            tag_i = 0;
        } else {
            // idx_max entry found for this tag, check next tag 
            search->tag_search_idx[tag_i] -= 1;
            tag_i += 1;
            
            // return if all tags found for this entry
            if (tag_i == tag_count) {
                ret = read_metadata(buf, idx_max);
                break;
            }
        }
    }
    
    for (U32 i = 0; i < search->tag_count; ++i)
        tag_entry_table_close(tag_entry_tables[i], &search->tag_entry[i]);
    
    return ret;
}

static inline U32 round_up_pow2(U32 n) {
    n--; 
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n++;
    return n;
}

static void init_clip_tables(void) {
    mkdir(clipsdir, S_IRWXU);
    mkdir(tagdir, S_IRWXU);

    tagdir_fd = open(tagdir, O_RDONLY, 0);
    meta_fd = open(metapath, O_RDWR|O_APPEND|O_CREAT, S_IRUSR|S_IWUSR);
    
    struct dirent **entries;
    int ret = scandir(tagdir, &entries, filter_file, NULL);
    if (ret < 0) {
        while (1);
        // IDK WHAT TO DO HERE TODO TODO TODO TODO --------------------------------------
    }
    U32 filenum = (U32)ret;
    
    // PLEASE be enough... TODO check for overflow
    tag_table_cap = filenum * 4;
    if (tag_table_cap < 4096)
        tag_table_cap = 4096;
    tag_table_cap = round_up_pow2(tag_table_cap);
    tag_table = calloc(tag_table_cap, sizeof(*tag_table));
    
    // PLEASE be enough... TODO check for overflow
    autocomplete_table_cap = filenum * 2 * 8;
    autocomplete_table_cap = round_up_pow2(autocomplete_table_cap);
    if (autocomplete_table_cap < 4096)
        autocomplete_table_cap = 4096;
    U32 autocomplete_table_size = autocomplete_table_cap * sizeof(*autocomplete_table);
    autocomplete_table = calloc(autocomplete_table_size, 1);
    
    // We store file_i initially, then convert that to char postfixes later.
    typedef struct AutoCompleteTempList {
        Str tag_prefix;
        AutoCompleteHashEntry *entry;
        struct {
            U32 postfix_table_size;
            U32 file_i;
        } top[10];
    } AutoCompleteTempList;
    
    // I don't feel like counting
    AutoCompleteTempList *temp_list = mmap(NULL, 1U << 30, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (temp_list == MAP_FAILED) {
        while (1);
        // IDK WHAT TO DO HERE TODO TODO TODO TODO --------------------------------------
    }
    U32 temp_list_size = 0;

    for (U32 file_i = 0; file_i < filenum; ++file_i) {
        char *filename = entries[file_i]->d_name;
        Str tag_name = { filename, strlen(filename) };
        
        struct stat stat;
        ret = fstatat(tagdir_fd, filename, &stat, 0);
        if (ret < 0) {
            while (1);
            // IDK WHAT TO DO HERE TODO TODO TODO TODO --------------------------------------
        }
        U32 tag_size = (U32)stat.st_size / sizeof(MetaIdx);

        TagEntry *tag = lookup_tag(tag_name);
        tag->table_size = tag_size;

        // fill out autocomplete table for each prefix in tag
        for (USize prefix_len = 0; prefix_len <= tag_name.len; ++prefix_len) {
            Str tag_prefix = { tag_name.buf, prefix_len };
            HashKey key = hash_bytes((U8*)tag_prefix.buf, tag_prefix.len);
            
            // lookup autocomplete table entry
            AutoCompleteTempList *temp;
            AutoCompleteHashEntry *entry;
            HashKey idx = key & (autocomplete_table_cap-1);
            while (1) {
                entry = &autocomplete_table[idx];
                if (entry->hash == key) {
                    // use existing autocomplete list
                    temp = &temp_list[entry->list_idx];
                    break;
                } else if (entry->hash == 0) {
                    // create new autocomplete list
                    entry->hash = key;
                    entry->list_idx = temp_list_size++;
                    temp = &temp_list[entry->list_idx];
                    temp->tag_prefix = tag_prefix;
                    temp->entry = entry;
                    break;
                }
                idx = (idx + 1) & (autocomplete_table_cap-1);
            }
            
            // if smaller than smallest entry
            if (tag_size > temp->top[0].postfix_table_size) {
                // insert maintaining ascending sorted order
                USize insert_i = 0;
                for (; insert_i != 9; insert_i++) {
                    if (tag_size <= temp->top[insert_i+1].postfix_table_size)
                        break;
                    temp->top[insert_i] = temp->top[insert_i+1];
                }
                temp->top[insert_i].postfix_table_size = tag_size;
                temp->top[insert_i].file_i = file_i;
            }
        }
    }
    
    // calculate size
    USize autocomplete_lists_size = 0;
    for (USize temp_i = 0; temp_i < temp_list_size; ++temp_i) {
        AutoCompleteTempList *temp = &temp_list[temp_i];
        
        autocomplete_lists_size += sizeof(U32); // entry count
        
        for (USize entry_i = 0; entry_i < 10; ++entry_i) {
            U32 postfix_table_size = temp->top[9 - entry_i].postfix_table_size;
            if (postfix_table_size == 0)
                break;

            const char *tag_name = entries[temp->top[9 - entry_i].file_i]->d_name;
            const char *tag_postfix = tag_name + temp->tag_prefix.len;
            U32 tag_len = (U32)strlen(tag_postfix);
            autocomplete_lists_size += sizeof(U32) // tag entry count
                + tag_len // tag name
                + 1; // null byte
        }
        
        autocomplete_lists_size = (autocomplete_lists_size + 3U) & ~3U; // alignment
    }
    
    // Convert temp table to actual table.
    autocomplete_lists = malloc(autocomplete_lists_size);

    autocomplete_lists_size = 0;
    for (USize temp_i = 0; temp_i < temp_list_size; ++temp_i) {
        AutoCompleteTempList *temp = &temp_list[temp_i];
        temp->entry->list_idx = (U32)autocomplete_lists_size;

        // fill out entry count
        U32 *entry_count = (U32*)&autocomplete_lists[autocomplete_lists_size];
        *entry_count = 0;
        autocomplete_lists_size += sizeof(U32);
        
        // fill out tag table sizes
        for (USize entry_i = 0; entry_i < 10; ++entry_i) {
            U32 postfix_table_size = temp->top[9 - entry_i].postfix_table_size;
            if (postfix_table_size == 0)
                break;
            (*entry_count)++;
            
            U32 *entry_table_size = (U32*)&autocomplete_lists[autocomplete_lists_size];
            *entry_table_size = postfix_table_size;
            autocomplete_lists_size += sizeof(U32);
        }
        
        // fill out tag postfixes
        for (USize entry_i = 0; entry_i < 10; ++entry_i) {
            U32 postfix_table_size = temp->top[9 - entry_i].postfix_table_size;
            if (postfix_table_size == 0)
                break;
            
            const char *tag_name = entries[temp->top[9 - entry_i].file_i]->d_name;
            const char *tag_postfix = tag_name + temp->tag_prefix.len;
            
            while (true) {
                char c = *(tag_postfix)++;
                autocomplete_lists[autocomplete_lists_size++] = c;
                if (c == 0)
                    break;
            } 
        }
        
        // align to 4 for next U32
        autocomplete_lists_size = (autocomplete_lists_size + 3U) & ~3U;
    }
    
    for (U32 file_i = 0; file_i < filenum; ++file_i)
        free(entries[file_i]);
        
    free(entries);
    munmap(temp_list, 1U << 30);
}
