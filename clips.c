// TODO: comprehensive testing
// TODO: write to disk
// TODO: reallocation

typedef U32 HashKey;
typedef U32 EntryIdx;
typedef EntryIdx *EntryTable;

static struct timespec timer;
static void timer_start(void) {
    clock_gettime(CLOCK_MONOTONIC, &timer);
}
static void timer_elapsed(const char *label) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    t.tv_sec -= timer.tv_sec;
    t.tv_nsec -= timer.tv_nsec;
    
    F64 time_ms = (F64)t.tv_sec * 1000.0 + (F64)t.tv_nsec / 1000000.0;
    printf("%s: %.2fms\n", label, time_ms);
}

typedef struct TagEntry {
    HashKey hash;
    U32 table_size;
    U32 name_idx;
    EntryTable table;
} TagEntry;

static HashKey hash_bytes(const U8* key, U64 len);
static TagEntry *lookup_tag(Str tag);

#define TABLESIZE (1024*1024)
static TagEntry *tag_table;

typedef struct Entry {
    U32 meta_idx;
    U32 tag_strings_idx;
} Entry;

#define METASIZE (10*1024*1024)
static char *meta;
static U32 meta_size;
static Entry *entry_table;
static U32 tag_strings_size;
static char *tag_strings;

static U32 tag_names_size;
static char *tag_names;

static U32 meta_entry_count = 0; // skip 0 (null) entry

#define AUTOCOMPLETE_LISTS_SIZE (10*1024*1024)
#define AUTOCOMPLETE_TABLE_SIZE (1*1024*1024)

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

AutoCompleteLookup tag_autocomplete(Str tag) {
    AutoCompleteLookup ret = { 0 };

    AutoCompleteHashEntry *entry; 
    HashKey key = hash_bytes((U8 *)tag.buf, tag.len);
    HashKey idx = key & (AUTOCOMPLETE_TABLE_SIZE-1);
    while (1) {
        entry = &autocomplete_table[idx];
        if (entry->hash == key) {
            break;
        } else if (entry->hash == 0) {
            // not found
            return ret;
        }
        idx = (idx + 1) & (AUTOCOMPLETE_TABLE_SIZE-1);
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

void remake_tag_autocomplete(void) {
    memset(autocomplete_lists, 0, AUTOCOMPLETE_LISTS_SIZE*sizeof(*autocomplete_lists));
    memset(autocomplete_table, 0, AUTOCOMPLETE_TABLE_SIZE*sizeof(*autocomplete_table));
    
    // We store hashes initially, then convert those to char postfixes later.
    typedef struct AutoCompleteTempList {
        Str tag_prefix;
        AutoCompleteHashEntry *entry;
        struct {
            U32 postfix_table_size;
            U32 tag_name_idx;
        } top[10];
    } AutoCompleteTempList;
    
    U32 temp_list_size = 0;
    AutoCompleteTempList *temp_list = calloc(AUTOCOMPLETE_LISTS_SIZE, sizeof(*temp_list));
    
    // fill out temp list with most common postfixes
    USize tag_name_i = 0;
    while (tag_name_i < tag_names_size) {
        Str tag_name = { &tag_names[tag_name_i], strlen(&tag_names[tag_name_i]) };
        tag_name_i += tag_name.len + 1;
        TagEntry *tag = lookup_tag(tag_name);
        U32 tag_table_size = tag->table_size;
        U32 tag_name_idx = tag->name_idx;
        
        // fill out autocomplete table for each prefix in tag
        for (USize prefix_len = 0; prefix_len <= tag_name.len; ++prefix_len) {
            Str tag_prefix = { tag_name.buf, prefix_len };
            HashKey key = hash_bytes((U8*)tag_prefix.buf, tag_prefix.len);
            
            // lookup autocomplete table entry
            AutoCompleteTempList *temp;
            AutoCompleteHashEntry *entry;
            HashKey idx = key & (AUTOCOMPLETE_TABLE_SIZE-1);
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
                idx = (idx + 1) & (AUTOCOMPLETE_TABLE_SIZE-1);
            }
            
            // if smaller than smallest entry
            if (tag_table_size > temp->top[0].postfix_table_size) {
                // insert maintaining ascending sorted order
                USize insert_i = 0;
                for (; insert_i != 9; insert_i++) {
                    if (tag_table_size <= temp->top[insert_i+1].postfix_table_size)
                        break;
                    temp->top[insert_i] = temp->top[insert_i+1];
                }
                temp->top[insert_i].postfix_table_size = tag_table_size;
                temp->top[insert_i].tag_name_idx = tag_name_idx;
            }
        }
    }
    
    /*
    Convert temp table to actual table.
    
    The temp table stores name_indices instead of postfixes.
    This is ok, but cache and space inefficent (tag_table->tag_name_table)*10.
    The actual table stores them inline.
    This is better for caching but can only be created after sorting the prefixes
    Because we don't know the final sizes of the post fixes.
    */
    
    U32 autocomplete_lists_size = 0;
    for (USize temp_i = 0; temp_i < temp_list_size; ++temp_i) {
        AutoCompleteTempList *temp = &temp_list[temp_i];
        
        temp->entry->list_idx = autocomplete_lists_size;

        // fill out entry count
        U32 *entry_count = (U32*)&autocomplete_lists[autocomplete_lists_size];
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
            char *tag_name = &tag_names[temp->top[9 - entry_i].tag_name_idx];
            char *tag_postfix = tag_name + temp->tag_prefix.len;
            
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
}

#define TAGMAX 32
typedef struct EntrySearch {
    U32 tag_count;
    TagEntry tag_entry[TAGMAX];
    U32 tag_search_idx[TAGMAX];
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

static TagEntry *lookup_tag(Str tag) {
    // hash tag
    HashKey key = hash_bytes((const U8*)tag.buf, tag.len);
    
    // lookup hash
    HashKey idx = key & (TABLESIZE-1);
    TagEntry *entry;
    
    while (1) {
        entry = &tag_table[idx];
        if (entry->hash == key) {
            break;
        } else if (entry->hash == 0) {
            // create new tag table
            entry->hash = key;
            entry->table = calloc(TABLESIZE, sizeof(*entry->table));
            entry->name_idx = tag_names_size;

            memcpy(tag_names + tag_names_size, tag.buf, tag.len);
            tag_names_size += (U32)tag.len;
            tag_names[tag_names_size++] = 0;
            
            break;
        }
        idx = (idx + 1) & (TABLESIZE-1);
    }
    
    return entry;
}

static void tag_entry(EntryIdx entry_idx, Str tag) {
    TagEntry *tag_entry = lookup_tag(tag);
    tag_entry->table[tag_entry->table_size++] = entry_idx;

    memcpy(tag_strings + tag_strings_size, tag.buf, tag.len);
    tag_strings_size += (U32)tag.len;
    tag_strings[tag_strings_size++] = '\n';
}

static char *entry_metadata(Entry *entry) {
    return &meta[entry->meta_idx];
}

static char *entry_tags(Entry *entry) {
    return &tag_strings[entry->tag_strings_idx];
}

static EntryIdx create_entry(Str new_meta) {
    EntryIdx entry_idx = meta_entry_count++;
    
    tag_strings_size++; // leave previous tag strings null-terminated
    entry_table[entry_idx] = (Entry) { meta_size, tag_strings_size };
    
    // copy metadata immediately
    U32 new_meta_len = (U32)new_meta.len;
    memcpy(meta + meta_size, new_meta.buf, new_meta_len);
    meta_size += new_meta_len;
    
    return entry_idx;
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
        i -= 1;
    }
    search->tag_entry[i] = *tag_entry;
    search->tag_count++;
}

static Entry *search_next_tagless(EntrySearch *search) {
    // Since there are no tags, this is just sorting by newest.
    // We just use the first search_idx as an index into meta entries.
    
    EntryIdx search_i = search->tag_search_idx[0];
    if (search_i >= meta_entry_count)
        return NULL;

    search->tag_search_idx[0]++;
    EntryIdx i = meta_entry_count - search_i - 1;
    return &entry_table[i];
}

static Entry *search_prev_tagless(EntrySearch *search) {
    // Since there are no tags, this is just sorting by newest.
    // We just use the first search_idx as an index into meta entries.
    
    EntryIdx search_i = search->tag_search_idx[0];
    if (search_i == 0)
        return NULL;
    
    search_i--;
    search->tag_search_idx[0] = search_i;
    EntryIdx i = meta_entry_count - search_i - 1;
    return &entry_table[i];
}

static Entry *search_next(EntrySearch *search) {
    if (search->tag_count == 0)
        return search_next_tagless(search);

    // TODO - when should we binary search instead of linear scan?

    EntryIdx idx_min = 0xFFFFFFFF;
    
    U32 tag_i = 0;
    U32 tag_count = search->tag_count;
    while (1) {
        TagEntry *tag_entry = &search->tag_entry[tag_i];
        U32 tag_entry_count = tag_entry->table_size;
        U32 tag_search_idx = search->tag_search_idx[tag_i];
        if (tag_search_idx >= tag_entry_count)
            return NULL;
        U32 tag_entry_idx = tag_entry_count - tag_search_idx - 1;
        
        EntryIdx prev_tag_entry = tag_entry->table[tag_entry_idx];
        
        if (prev_tag_entry > idx_min) {
            // try next entry for this tag
            search->tag_search_idx[tag_i] += 1;
        } else if (prev_tag_entry < idx_min) {
            // idx_min entry not found for this tag, start again 
            idx_min = prev_tag_entry;
            tag_i = 0;
        } else {
            // idx_min entry found for this tag, check next tag 
            tag_i += 1;
            
            // return if all tags found for this entry
            if (tag_i == tag_count) {
                search->tag_search_idx[0] += 1;
                break;
            }
        }
    }
    return &entry_table[idx_min];
}

static Entry *search_prev(EntrySearch *search) {
    if (search->tag_count == 0)
        return search_prev_tagless(search);

    // TODO - when should we binary search instead of linear scan?

    EntryIdx idx_max = 0;
    
    U32 tag_count = search->tag_count;
    
    // clamp
    for (USize i = 0; i < tag_count; ++i) {
        TagEntry *tag_entry = &search->tag_entry[i];
        U32 tag_entry_count = tag_entry->table_size;
        if (tag_entry_count == 0)
            return NULL;

        U32 tag_search_idx = search->tag_search_idx[i];
        if (tag_search_idx >= tag_entry_count)
            search->tag_search_idx[i] = tag_entry_count - 1;
    }
    
    USize tag_i = 0;
    while (1) {
        TagEntry *tag_entry = &search->tag_entry[tag_i];
        U32 tag_entry_count = tag_entry->table_size;
        U32 tag_search_idx = search->tag_search_idx[tag_i];
        if (tag_search_idx == 0)
            return NULL;

        U32 tag_entry_idx = tag_entry_count - tag_search_idx - 1;
        EntryIdx prev_tag_entry = tag_entry->table[tag_entry_idx];
        
        if (prev_tag_entry < idx_max) {
            // try prev entry for this tag
            search->tag_search_idx[tag_i] -= 1;
        } else if (prev_tag_entry > idx_max) {
            // idx_max entry not found for this tag, start again 
            idx_max = prev_tag_entry;
            tag_i = 0;
        } else {
            // idx_max entry found for this tag, check next tag 
            tag_i += 1;
            
            // return if all tags found for this entry
            if (tag_i == tag_count) {
                search->tag_search_idx[0] -= 1;
                break;
            }
        }
    }
    return &entry_table[idx_max];
}

static void init_clip_tables(void) {
    tag_table = calloc(TABLESIZE, sizeof(*tag_table));
    entry_table = calloc(TABLESIZE, sizeof(*entry_table));
    meta = calloc(METASIZE, sizeof(*meta));
    tag_strings = calloc(METASIZE, sizeof(*tag_strings));
    tag_names = calloc(METASIZE, sizeof(*tag_names));
    autocomplete_lists = calloc(AUTOCOMPLETE_LISTS_SIZE, sizeof(*autocomplete_lists));
    autocomplete_table = calloc(AUTOCOMPLETE_TABLE_SIZE, sizeof(*autocomplete_table));
}

// populate db for testing
__attribute__((unused))
static void create_entries(U32 count, U32 tag_count) {
    char metabuf[13] = { 0, 'e', ' ',    0, 0, 0, 0, 0, 0, 0, 0,    0, 0 };
    char tagbuf[3] = { 0 };
    U32 rng = 0x67236;
    
    for (U32 i = 0; i < count; ++i) {
        U32 n = i;
        for (U32 j = 0; j < 8; ++j) { 
            metabuf[3 + j] = 'a' + (char)(n >> 28);
            n <<= 4;
        }
        
        Str metadata = { metabuf, 13 };
        EntryIdx entry = create_entry(metadata);

        for (U32 j = 0; j < tag_count; ++j) {
            rng = hash_bytes((void*)&rng, sizeof(rng));
            tagbuf[0] = 'a' + (char)((rng >> 4) & 0xf);
            tagbuf[1] = 'a' + (char)((rng >> 8) & 0xf);

            Str tag = { tagbuf, 2 };
            tag_entry(entry, tag);
        }
    }
}

/*int main(void) {
    init_clip_tables();
    
    timer_start();
    create_entries(1 << 18, 16);
    timer_elapsed("create entries");

    // EntryIdx entry1 = create_entry("test 1!");
    // tag_entry(entry1, "tag1");
    // tag_entry(entry1, "tag2");

    // EntryIdx entry2 = create_entry("test 2!");
    // tag_entry(entry2, "tag1");
    // tag_entry(entry2, "tag3");
    
    EntrySearch search = { 0 };
    search_tag(&search, "ab");
    search_tag(&search, "ac");
    search_tag(&search, "ba");
    // search_tag(&search, "ad");
    
    timer_start();
    while (1) {
        const char *metadata = search_next(&search);
        if (metadata == 0)
            break;
        printf("found '%s'\n", metadata);
    }
    timer_elapsed("search");
    
    return 0;
}*/

