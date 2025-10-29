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
#define META_NULL 0xFFFFFFFFU

static Str all = ConstStr("!all");
static Str reported_spam = ConstStr("!reported_spam");
static Str reported_dup = ConstStr("!reported_dup");
static Str reported_error = ConstStr("!reported_err");

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
#define TAG_LEN_MAX 31
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

static MetaIdx *tag_entry_table_get_inner(TagEntry *tag_entry, Str tag_name, bool writable) {
    if (tag_entry->table_size == 0)
        return NULL;

    char temp_pathname[TAG_LEN_MAX+1];
    memcpy(temp_pathname, tag_name.buf, tag_name.len);
    temp_pathname[tag_name.len] = 0;
    
    int fd = openat(tagdir_fd, temp_pathname, writable ? O_RDWR : O_RDONLY, S_IRUSR | S_IWUSR);
    if (fd < 0)
        return NULL;
    
    MetaIdx *table = mmap(
        NULL,
        tag_entry->table_size * sizeof(U32),
        writable ? (PROT_READ | PROT_WRITE) : PROT_READ,
        writable ? MAP_SHARED : MAP_PRIVATE,
        fd, 0
    );

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

static MetaIdx *tag_entry_table_get_rw(TagEntry *tag_entry, Str tag_name) {
    return tag_entry_table_get_inner(tag_entry, tag_name, true);
}

static MetaIdx *tag_entry_table_get(TagEntry *tag_entry, Str tag_name) {
    return tag_entry_table_get_inner(tag_entry, tag_name, false);
}

static void tag_entry_table_close(MetaIdx *table, TagEntry *tag_entry) {
    munmap(table, tag_entry->table_size);
}

static void realloc_tag_table(void) {
    U32 new_tag_table_cap = tag_table_cap * 2;
    
    TagEntry *new_table = calloc(new_tag_table_cap, sizeof(*tag_table));
    
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
    
    free(tag_table);
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
        printf("Failed to open tag file for '%.*s'\n", (int)tag.len, tag.buf);
        return;
    }
    
    USize written = 0;
    while (written != 4) {
        ssize_t ret = write(fd, (char*)&meta_idx + written, sizeof(meta_idx) - written);
        if (ret == -1) {
            printf("Failed to append tag file for '%.*s'\n", (int)tag.len, tag.buf);
            close(fd);
            return;
        }
        written += (USize)ret;
    }
    
    tag_entry->table_size++;
    close(fd);
}

static MetaIdx create_entry(Str new_meta) {
    off_t seek_ret = lseek(meta_fd, 0, SEEK_END);
    if (seek_ret < 0) {
        printf("Failed to seek metadata file\n");
        return META_NULL;
    }
    MetaIdx meta_idx = (MetaIdx)seek_ret;
    
    ssize_t ret = write(meta_fd, new_meta.buf, new_meta.len);
    if (ret != (ssize_t)new_meta.len) {
        printf("Failed to append metadata file\n");
        return META_NULL;
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

enum {
    REPORT_SPAM,
    REPORT_DUPLICATE,
    REPORT_ERROR,

    REPORT_NONE = -1,
};

// pretty inefficient, but there shouldn't be too many reported
static void check_reported(U32 *reasons, MetaIdx *meta_indices, USize count) {
    for (USize i = 0; i < count; ++i)
        reasons[i] = 0;

    TagEntry *te_spam = lookup_tag(reported_spam);
    TagEntry *te_dup = lookup_tag(reported_dup);
    TagEntry *te_err = lookup_tag(reported_error);
    
    if (te_spam == NULL || te_dup == NULL || te_dup == NULL)
        return;

    MetaIdx *tb_spam = tag_entry_table_get(te_spam, reported_spam);
    if (tb_spam) {
        for (USize i = 0; i < te_spam->table_size; ++i) {
            MetaIdx spam = tb_spam[i];
            for (USize j = 0; j < count; ++j)
                if (spam == meta_indices[j])
                    reasons[j] |= 1u << REPORT_SPAM;
        }
        tag_entry_table_close(tb_spam, te_spam);
    }
    
    MetaIdx *tb_dup = tag_entry_table_get(te_dup, reported_dup);
    if (tb_dup) {
        for (USize i = 0; i < te_dup->table_size; ++i) {
            MetaIdx dup = tb_dup[i];
            for (USize j = 0; j < count; ++j)
                if (dup == meta_indices[j])
                    reasons[j] |= 1u << REPORT_DUPLICATE;
        }
        tag_entry_table_close(tb_dup, te_dup);
    }
    
    MetaIdx *tb_err = tag_entry_table_get(te_err, reported_error);
    if (tb_err) {
        for (USize i = 0; i < te_err->table_size; ++i) {
            MetaIdx err = tb_err[i];
            for (USize j = 0; j < count; ++j)
                if (err == meta_indices[j])
                    reasons[j] |= 1u << REPORT_ERROR;
        }
        tag_entry_table_close(tb_err, te_err);
    }
}

static void report_clip(MetaIdx meta_idx, int reason) {
    U32 prev_reports;
    check_reported(&prev_reports, &meta_idx, 1);

    Str report_name;
    if (reason == REPORT_SPAM) {
        if (prev_reports & (1u << REPORT_SPAM))
            return;
        report_name = reported_spam;
    } else if (reason == REPORT_DUPLICATE) {
        if (prev_reports & (1u << REPORT_DUPLICATE))
            return;
        report_name = reported_dup;
    } else if (reason == REPORT_ERROR) {
        if (prev_reports & (1u << REPORT_ERROR))
            return;
        report_name = reported_error;
    } else {
        return;
    }
    
    // prevent mass reporting
    TagEntry *te = lookup_tag(report_name);
    if (te == NULL) return;
    if (te->table_size >= 256) return;

    tag_entry(meta_idx, report_name);
}

static void unreport_clip(MetaIdx meta_idx, int reason) {
    Str report_name;
    if (reason == REPORT_SPAM) {
        report_name = reported_spam;
    } else if (reason == REPORT_DUPLICATE) {
        report_name = reported_dup;
    } else if (reason == REPORT_ERROR) {
        report_name = reported_error;
    } else {
        return;
    }
    
    TagEntry *te = lookup_tag(report_name);
    if (te == NULL) return;
    
    MetaIdx *tag_entry_table = tag_entry_table_get_rw(te, report_name);
    if (tag_entry_table == NULL) return;
    
    for (U32 i = 0; i < te->table_size; ++i) {
        if (tag_entry_table[i] == meta_idx) {
            U32 copy_num = te->table_size - i - 1;
            memmove(
                &tag_entry_table[i],
                &tag_entry_table[i+1],
                copy_num * sizeof(MetaIdx)
            );
            te->table_size--;
            break;
        }
    }

    tag_entry_table_close(tag_entry_table, te);
    
    // truncate file
    char temp_pathname[TAG_LEN_MAX+1];
    memcpy(temp_pathname, report_name.buf, report_name.len);
    temp_pathname[report_name.len] = 0;
    
    int fd = openat(tagdir_fd, temp_pathname, O_WRONLY, S_IRUSR|S_IWUSR);
    if (fd < 0) return;
    ftruncate(fd, te->table_size * sizeof(MetaIdx));
    close(fd);
}

// TODO single tag optimization?

static MetaIdx search_next_tagless(EntrySearch *search) {
    // sort by newest using the all tag
    
    TagEntry *tag_entry = lookup_tag(all);
    MetaIdx search_i = search->tag_search_idx[0];
    if (search_i >= tag_entry->table_size)
        return META_NULL;

    MetaIdx *tag_entry_table = tag_entry_table_get(tag_entry, all);
    if (tag_entry_table == NULL)
        return META_NULL;
    
    search->tag_search_idx[0] = search_i + 1;
    U32 i = tag_entry->table_size - search_i - 1;
    MetaIdx meta_i = tag_entry_table[i];
    tag_entry_table_close(tag_entry_table, tag_entry);

    return meta_i;
}

static MetaIdx search_prev_tagless(EntrySearch *search) {
    // sort by newest using the all tag
    
    MetaIdx search_i = search->tag_search_idx[0];
    if (search_i == 0)
        return META_NULL;

    TagEntry *tag_entry = lookup_tag(all);
    MetaIdx *tag_entry_table = tag_entry_table_get(tag_entry, all);
    if (tag_entry_table == NULL)
        return META_NULL;
    
    if (search_i > tag_entry->table_size)
        search_i = tag_entry->table_size;

    search_i--;
    search->tag_search_idx[0] = search_i;
    U32 i = tag_entry->table_size - search_i - 1;
    MetaIdx meta_i = tag_entry_table[i];
    tag_entry_table_close(tag_entry_table, tag_entry);
    
    return meta_i;
}

static MetaIdx search_next(EntrySearch *search) {
    if (search->tag_count == 0)
        return search_next_tagless(search);
    
    // clamp
    for (USize i = 0; i < search->tag_count; ++i) {
        TagEntry *tag_entry = &search->tag_entry[i];
        U32 tag_entry_count = tag_entry->table_size;
        if (tag_entry_count == 0)
            return META_NULL;

        U32 tag_search_idx = search->tag_search_idx[i];
        if (tag_search_idx >= tag_entry_count)
            search->tag_search_idx[i] = tag_entry_count;
    }
    
    MetaIdx *tag_entry_tables[TAG_MAX];
    for (U32 i = 0; i < search->tag_count; ++i) {
        tag_entry_tables[i] = tag_entry_table_get(
            &search->tag_entry[i],
            search->tag_names[i]
        );
        
        if (tag_entry_tables[i] == NULL) {
            for (; i != 0; --i)
                tag_entry_table_close(tag_entry_tables[i-1], &search->tag_entry[i-1]);
            return META_NULL;
        }
    }
    
    // TODO - when should we binary search instead of linear scan?
    MetaIdx idx_min = 0xFFFFFFFF;
    
    MetaIdx ret;
    U32 tag_i = 0;
    U32 tag_count = search->tag_count;
    while (1) {
        TagEntry *tag_entry = &search->tag_entry[tag_i];
        U32 tag_entry_count = tag_entry->table_size;
        U32 tag_search_idx = search->tag_search_idx[tag_i];
        if (tag_search_idx >= tag_entry_count) {
            ret = META_NULL;
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
                ret = idx_min;
                break;
            }
        }
    }
    
    for (U32 i = 0; i < search->tag_count; ++i)
        tag_entry_table_close(tag_entry_tables[i], &search->tag_entry[i]);
        
    return ret;
}

static MetaIdx search_prev(EntrySearch *search) {
    if (search->tag_count == 0)
        return search_prev_tagless(search);

    // clamp
    for (USize i = 0; i < search->tag_count; ++i) {
        TagEntry *tag_entry = &search->tag_entry[i];
        U32 tag_entry_count = tag_entry->table_size;
        if (tag_entry_count == 0)
            return META_NULL;

        U32 tag_search_idx = search->tag_search_idx[i];
        if (tag_search_idx >= tag_entry_count)
            search->tag_search_idx[i] = tag_entry_count;
    }
    
    MetaIdx *tag_entry_tables[TAG_MAX];
    for (U32 i = 0; i < search->tag_count; ++i) {
        tag_entry_tables[i] = tag_entry_table_get(
            &search->tag_entry[i],
            search->tag_names[i]
        );
        
        if (tag_entry_tables[i] == NULL) {
            for (; i != 0; --i)
                tag_entry_table_close(tag_entry_tables[i-1], &search->tag_entry[i-1]);
            return META_NULL;
        }
    }
    
    // TODO - when should we binary search instead of linear scan?
    MetaIdx idx_max = 0;
    U32 tag_count = search->tag_count;
    
    USize tag_i = 0;
    MetaIdx ret;
    while (1) {
        TagEntry *tag_entry = &search->tag_entry[tag_i];
        U32 tag_entry_count = tag_entry->table_size;
        U32 tag_search_idx = search->tag_search_idx[tag_i];
        
        if (tag_search_idx == 0) {
            ret = META_NULL;
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
                ret = idx_max;
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
        printf("Failed to init clip clip tables: scandir failed.\n");
        exit(1);
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
    AutoCompleteTempList *temp_list = mmap(NULL, 1U << 29, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (temp_list == MAP_FAILED) {
        printf("Failed to init clip clip tables: map failed.\n");
        exit(1);
    }
    U32 temp_list_size = 0;

    for (U32 file_i = 0; file_i < filenum; ++file_i) {
        char *filename = entries[file_i]->d_name;
        Str tag_name = { filename, strlen(filename) };
        
        struct stat stat;
        ret = fstatat(tagdir_fd, filename, &stat, 0);
        if (ret < 0) {
            printf("Failed to init clip clip tables: fstatat failed.\n");
            exit(1);
        }
        U32 tag_size = (U32)stat.st_size / sizeof(MetaIdx);

        TagEntry *tag = lookup_tag(tag_name);
        tag->table_size = tag_size;
        
        // ignore special tags - e.g. !all
        if (tag_name.len != 0 && tag_name.buf[0] == '!')
            continue;

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
    munmap(temp_list, 1U << 29);
}
