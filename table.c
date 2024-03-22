#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void initTable(Table* table) {
    table->count    = 0;
    table->capacity = 0;
    table->entries  = NULL;
}

void freeTable(Table* table) {
    FREE_ARRAY(Entry, table->entries, table->capacity);
    initTable(table);
}

/* Find an entry in the hashtable; create one if none exists.
 *
 * the map uses linear probing. In case of a collision, check 
 * successive buckets are checked for the value. If no entry
 * is found, a new one is initialized and it's pointer returned. */ 
static Entry* findEntry(Entry* entries, int capacity,
                        ObjString* key) {
    uint32_t index   = key->hash % capacity;
    Entry* tombstone = NULL;
 
    for (;;) {
        Entry* entry = &entries[index];
        
        if (entry->key == NULL) {
            if (IS_NIL(entry->value)) {
                // Empty entry.
                return tombstone != NULL ? tombstone : entry;
            } else {
                // We found a tombstone.
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (entry->key == key) {
            // We found the key
            return entry;
        }

        // TODO comparing strings with `==`!?
        if (entry->key == key || entry->key == NULL) {
            return entry;
        }   

        // Loop around to the start of the array.
        // Necessary to avoid trying to find the key outside of
        // the table's bounds in memory.
        index = (index + 1) % capacity;
    }
}

/* Create a new object from the retrieved value, if one exists.
 *
 * Return true if the key is found, otherwise false. */
bool tableGet(Table* table, ObjString* key, Value* value) {
    if (table->count == 0) return false;

    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;
    
    *value = entry->value;
    return true;
}

/* Delete a key from the table
 * 
 * Returns true if a key was tombstoned, false otherwise. */
bool tableDelete(Table* table, ObjString* key) {
    if (table->count == 0) return false;

    // Does the entry exist?
    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;

    // Create a sentinel 'tombstone' entry.
    // This is because linear probing may require this entry to
    // be set in order to find subsequent values properly.
    entry->key   = NULL;
    entry->value = BOOL_VAL(true);
  
    return true;
}

static void adjustCapacity(Table* table, int capacity) {
    Entry* entries = ALLOCATE(Entry, capacity);
    for (int i = 0; i< capacity; i++) {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }
    table->entries  = entries;
    table->capacity = capacity;

    for (int i = 0; i < table->capacity; ++i) {
        Entry* entry = &table->entries[i];
        // There was no entry in our old table here, continue. 
        if (entry->key == NULL) continue;

        Entry* dest = findEntry(entries, capacity, entry->key);
        dest->key   = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    FREE_ARRAY(Entry, table->entries, table->capacity);
    table->entries  = entries;
    table->capacity = capacity;
}

/* Insert a key-value pair into a hashtable */
bool tableSet(Table* table, ObjString* key, Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(table, capacity);
    }

    Entry* entry = findEntry(table->entries, table->capacity, key);
    
    bool isNewKey = entry->key == NULL;
    if (isNewKey && IS_NIL(entry->value)) table->count++;
    
    entry->key   = key;
    entry->value = value;
    return isNewKey;
}

/* Copy the values from one table to another */
void tableAddAll(Table* from, Table* to) {
    for (int i = 0; i < from->capacity; ++i) {
        Entry* entry = &from->entries[i];
        if (entry->key != NULL) {
            tableSet(to, entry->key, entry->value);
        }
    }
}

ObjString* tableFindString(Table* table, const char* chars,
                          int length, uint32_t hash) {
    if (table->count == 0) return NULL;

    uint32_t index = hash % table->capacity;
    
    for (;;) {
        Entry* entry = &table->entries[index];
        if (entry->key == NULL) {
            // Stop if we find an empty, non-tombstone, entry.
            if (IS_NIL(entry->value)) return NULL;
        } else if (entry->key->length == length &&
                   memcmp(entry->key->chars, chars, length) == 0) {
            // Match found!
            return entry->key;
        }
        // Increment by one, looping around to the start if necessary
        index = (index + 1) % table->capacity;
    }
}