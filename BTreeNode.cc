#include <list>
#include <cstdio>
#include <cstdlib>
#include <string.h>
#include <math.h>
#include <iostream>
#include "BTreeNode.h"

using namespace std;

// As each node may have up to N-1 keys, N = 76
// ceil((N-1)/2) = 38
// Nodes may have [38, 75] keys
// ceil(N/2) = 38
// Non-leaf nodes may have [38, 75] keys
#define MAX_KEYS 75

void reportErrorExit(RC error) {
    printf("Error! Received RC code%d\n", error);
    exit(error);
}

BTLeafNode::BTLeafNode(PageId id) {
    isLeaf = 1;
    length = 0;
    this->id = id;
    nextLeaf = -1;
}

PageId BTLeafNode::getPageId() {
    return id;
}

PageId BTLeafNode::getNextLeaf() {
    return nextLeaf;
}

void BTLeafNode::print(std::string offset) {
    std::cout << offset << "Id: " << id;
    std::cout << "\tisLeaf: " << isLeaf;
    std::cout << "\tlength: " << length << std::endl;
    std::cout << offset << "Records/keys: " << std::endl;
    std::list<RecordId>::iterator recIt = records.begin();
    std::list<int>::iterator keyIt = keys.begin();
    for (int i = 0; i < length; i++) {
        std::cout << offset << "(" << recIt->pid << "," << recIt->sid << ") ";
        std::cout << *keyIt << std::endl;
        recIt++;
        keyIt++;
    }
    std::cout << offset << "nextLeaf: " << nextLeaf << std::endl;
}

/*
 * Read the content of the node from the page pid in the PageFile pf.
 * @param pid[IN] the PageId to read
 * @param pf[IN] PageFile to read from
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::read(PageId pid, const PageFile& pf)
{
    memset(buffer, 0, sizeof(char) * PageFile::PAGE_SIZE);
    RC errorCode = pf.read(pid, buffer);
    if (errorCode < 0)
        reportErrorExit(errorCode);

    int bufferIndex = 0;
    memcpy(&isLeaf, buffer + bufferIndex, sizeof(int));
    bufferIndex += sizeof(int);
    memcpy(&length, buffer + bufferIndex, sizeof(int));
    bufferIndex += sizeof(int);
    for (int i = 0; i < length; i++) {
        RecordId nextRecord;
        memcpy(&nextRecord, buffer + bufferIndex, sizeof(RecordId));
        bufferIndex += sizeof(RecordId);
        records.push_back(nextRecord);
        int nextKey;
        memcpy(&nextKey, buffer + bufferIndex, sizeof(int));
        bufferIndex += sizeof(int);
        keys.push_back(nextKey);
    }
    memcpy(&nextLeaf, buffer + bufferIndex, sizeof(PageId));
    return 0;
}
    
/*
 * Write the content of the node to the page pid in the PageFile pf.
 * @param pid[IN] the PageId to write to
 * @param pf[IN] PageFile to write to
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::write(PageId pid, PageFile& pf)
{
    memset(buffer, 0, sizeof(char) * PageFile::PAGE_SIZE);
    int bufferIndex = 0;
    memcpy(buffer + bufferIndex, &isLeaf, sizeof(int));
    bufferIndex += sizeof(int);
    memcpy(buffer + bufferIndex, &length, sizeof(int));
    bufferIndex += sizeof(int);
    std::list<RecordId>::iterator recIt = records.begin();
    std::list<int>::iterator keyIt = keys.begin();
    for (int i = 0; i < length; i++) {
        memcpy(buffer + bufferIndex, &*recIt, sizeof(RecordId));
        bufferIndex += sizeof(RecordId);
        memcpy(buffer + bufferIndex, &*keyIt, sizeof(int));
        bufferIndex += sizeof(int);
        recIt++;
        keyIt++;
    }
    memcpy(buffer + bufferIndex, &nextLeaf, sizeof(PageId));
    bufferIndex += sizeof(PageId);
    RC errorCode = pf.write(pid, buffer);
    if (errorCode < 0)
        reportErrorExit(errorCode);
    return 0;
}

/*
 * Return the number of keys stored in the node.
 * @return the number of keys in the node
 */
int BTLeafNode::getKeyCount()
{
    return length;
}

RC BTLeafNode::insertWithoutCheck(int key, const RecordId& rid)
{
    int index = 0;
    std::list<int>::iterator it;
    for(it = keys.begin(); it != keys.end(); it++) {
        if (*it < key) {
            index++;
        }
        else
            break;
    }
    keys.insert(it, key);
    std::list<RecordId>::iterator recIt = records.begin();
    for (int i = 0; i < index; i++) {
        recIt++;
    }
    RecordId newRec;
    newRec.pid = rid.pid;
    newRec.sid = rid.sid;
    records.insert(recIt, newRec);
    length++;
    return 0;
}

/*
 * Insert a (key, rid) pair to the node.
 * @param key[IN] the key to insert
 * @param rid[IN] the RecordId to insert
 * @return 0 if successful. Return an error code if the node is full.
 */
RC BTLeafNode::insert(int key, const RecordId& rid)
{
    if (length >= MAX_KEYS)
        return RC_NODE_FULL;
    else {
        return insertWithoutCheck(key, rid);
    }
}

RC BTLeafNode::insert_end(int key, const RecordId& rid)
{
    RecordId newRec;
    newRec.pid = rid.pid;
    newRec.sid = rid.sid;
    records.push_back(newRec);
    keys.push_back(key);
    length++;
    return 0;
}

/*
 * Insert the (key, rid) pair to the node
 * and split the node half and half with sibling.
 * The first key of the sibling node is returned in siblingKey.
 * @param key[IN] the key to insert.
 * @param rid[IN] the RecordId to insert.
 * @param sibling[IN] the sibling node to split with. This node MUST be EMPTY when this function is called.
 * @param siblingKey[OUT] the first key in the sibling node after split.
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::insertAndSplit(int key, const RecordId& rid, 
                              BTLeafNode& sibling, int& siblingKey)
{
    // Note: sibling must have been properly initialized by the caller, with only its lists missing
    if (length < MAX_KEYS)
        return RC_INVALID_RID;
    // The key and rid are first properly inserted into the lists to preserve ordering, before splitting between this node and sibling
    insertWithoutCheck(key, rid);
    int half = ceil(MAX_KEYS/2.0);
    std::list<int>::iterator keyIt = keys.begin();
    std::list<RecordId>::iterator recIt = records.begin();
    for (int i = 0; i < half; i++) {
        keyIt++;
        recIt++;
    }
    while (keyIt != keys.end()) {
        RC errorCode = sibling.insert_end(*keyIt, *recIt);
        if (errorCode < 0)
            return errorCode;
        keyIt = keys.erase(keyIt);
        recIt = records.erase(recIt);
        length--;
    }
    RecordId sibRec;
    sibling.setNextNodePtr(nextLeaf);
    nextLeaf = sibling.getPageId();
    RC errorCode = sibling.readEntry(0, siblingKey, sibRec);
    if (errorCode < 0)
        return errorCode;
    return 0;
}

/**
 * If searchKey exists in the node, set eid to the index entry
 * with searchKey and return 0. If not, set eid to the index entry
 * immediately after the largest index key that is smaller than searchKey,
 * and return the error code RC_NO_SUCH_RECORD.
 * Remember that keys inside a B+tree node are always kept sorted.
 * @param searchKey[IN] the key to search for.
 * @param eid[OUT] the index entry number with searchKey or immediately
                   behind the largest key smaller than searchKey.
 * @return 0 if searchKey is found. Otherwise return an error code.
 */
RC BTLeafNode::locate(int searchKey, int& eid)
{
    std::list<int>::iterator it = keys.begin();
    eid = 0;
    // If first entry is smaller search key cannot be found as key is increasing
    if (searchKey < *it) {
        return RC_NO_SUCH_RECORD;
    }
    // Loop continues while search key > last key checked
    for (; it != keys.end(); it++) {
        if (searchKey == *it)
            return 0;
        // If search key is less than current entry, cannot be found
        // eid has not been incremented and holds index of largest key smaller
        else if (searchKey < *it) {
            return RC_NO_SUCH_RECORD;
        }
        eid++;
    }
    eid--; // Undo last eid increment to meet eid value requirements
    return RC_NO_SUCH_RECORD;
}

/*
 * Read the (key, rid) pair from the eid entry.
 * @param eid[IN] the entry number to read the (key, rid) pair from
 * @param key[OUT] the key from the entry
 * @param rid[OUT] the RecordId from the entry
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::readEntry(int eid, int& key, RecordId& rid)
{
    // Note: node entries are indexed starting from zero, length starts from 1
    if (eid >= length || eid < 0)
        return RC_NO_SUCH_RECORD;
    
    std::list<int>::iterator keyIt = keys.begin();
    std::list<RecordId>::iterator recIt = records.begin();
    for (int i = 0; i < eid; i++) {
        keyIt++;
        recIt++;
    }
    key = *keyIt;
    rid = *recIt;
    return 0;
}

/*
 * Return the pid of the next slibling node.
 * @return the PageId of the next sibling node 
 */
PageId BTLeafNode::getNextNodePtr()
{
    return nextLeaf;
}

/*
 * Set the pid of the next slibling node.
 * @param pid[IN] the PageId of the next sibling node 
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::setNextNodePtr(PageId pid)
{
    nextLeaf = pid;
    return 0;
}

BTNonLeafNode::BTNonLeafNode(PageId id) {
    isLeaf = 0;
    length = 0;
    this->id = id;
}

PageId BTNonLeafNode::getPageId() {
    return id;
}

void BTNonLeafNode::setLastId(PageId last) {
    lastId = last;
}

PageId BTNonLeafNode::getLastId() {
    return lastId;
}

PageId BTNonLeafNode::readEntry(int eid) {
    int i = 0;
    for (std::list<PageId>::iterator it = pages.begin(); it != pages.end(); it++) {
        if (i == eid)
            return *it;
        i++;
	}
}

void BTNonLeafNode::print(std::string offset) {
    std::cout << offset << "Id: " << id;
    std::cout << "\tisLeaf: " << isLeaf;
    std::cout << "\tlength: "<< length << std::endl;
    std::cout << offset << "Pages/keys: " << std::endl;
    std::list<PageId>::iterator pageIt = pages.begin();
    std::list<int>::iterator keyIt = keys.begin();
    for (int i = 0; i < length; i++) {
        std::cout << offset << *pageIt << " " << *keyIt << std::endl;
        pageIt++;
        keyIt++;
    }
    std::cout << offset << "lastId: " << lastId << std::endl;
}

/*
 * Read the content of the node from the page pid in the PageFile pf.
 * @param pid[IN] the PageId to read
 * @param pf[IN] PageFile to read from
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::read(PageId pid, const PageFile& pf)
{
    memset(buffer, 0, sizeof(char) * PageFile::PAGE_SIZE);
    RC errorCode = pf.read(pid, buffer);
    if (errorCode < 0)
        reportErrorExit(errorCode);

    int bufferIndex = 0;
    memcpy(&isLeaf, buffer + bufferIndex, sizeof(int));
    bufferIndex += sizeof(int);
    memcpy(&length, buffer + bufferIndex, sizeof(int));
    bufferIndex += sizeof(int);
    for (int i = 0; i < length; i++) {
        PageId nextPage;
        memcpy(&nextPage, buffer + bufferIndex, sizeof(PageId));
        bufferIndex += sizeof(PageId);
        pages.push_back(nextPage);
        int nextKey;
        memcpy(&nextKey, buffer + bufferIndex, sizeof(int));
        bufferIndex += sizeof(int);
        keys.push_back(nextKey);
    }
    memcpy(&lastId, buffer + bufferIndex, sizeof(PageId));
    bufferIndex += sizeof(PageId);
    return 0;
}
    
/*
 * Write the content of the node to the page pid in the PageFile pf.
 * @param pid[IN] the PageId to write to
 * @param pf[IN] PageFile to write to
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::write(PageId pid, PageFile& pf)
{
    memset(buffer, 0, sizeof(char) * PageFile::PAGE_SIZE);
    int bufferIndex = 0;
    memcpy(buffer + bufferIndex, &isLeaf, sizeof(int));
    bufferIndex += sizeof(int);
    memcpy(buffer + bufferIndex, &length, sizeof(int));
    bufferIndex += sizeof(int);
    std::list<PageId>::iterator pageIt = pages.begin();
    std::list<int>::iterator keyIt = keys.begin();
    for (int i = 0; i < length; i++) {
        memcpy(buffer + bufferIndex, &*pageIt, sizeof(PageId));
        bufferIndex += sizeof(PageId);
        memcpy(buffer + bufferIndex, &*keyIt, sizeof(int));
        bufferIndex += sizeof(int);
        pageIt++;
        keyIt++;
    }
    memcpy(buffer + bufferIndex, &lastId, sizeof(PageId));
    bufferIndex += sizeof(PageId);
    RC errorCode = pf.write(pid, buffer);
    if (errorCode < 0)
        reportErrorExit(errorCode);
    return 0;
}

/*
 * Return the number of keys stored in the node.
 * @return the number of keys in the node
 */
int BTNonLeafNode::getKeyCount()
{ 
    return length;
}

RC BTNonLeafNode::insertWithoutCheck(int key, PageId pid)
{
    int index = 0;
    std::list<int>::iterator it;
    for (it = keys.begin(); it != keys.end(); it++) {
        if (*it < key) {
        index++;
        }
        else
            break;
    }
    // Special case; change lastId
    if (it == keys.end()) {
        keys.push_back(key);
        pages.push_back(lastId);
        lastId = pid;
        length++;
	    return 0;
    }
    keys.insert(it, key);
    std::list<PageId>::iterator pageIt = pages.begin();
    for (int i = 0; i < index; i++) {
        pageIt++;
    }
    pageIt++; // Page Ids inserted with key are inserted at index 1 higher
    pages.insert(pageIt, pid);
    length++;
    return 0;
}

/*
 * Insert a (key, pid) pair to the node.
 * @param key[IN] the key to insert
 * @param pid[IN] the PageId to insert
 * @return 0 if successful. Return an error code if the node is full.
 */
RC BTNonLeafNode::insert(int key, PageId pid)
{
    if (length >= MAX_KEYS)
        return RC_NODE_FULL;
    else {
        return insertWithoutCheck(key, pid);
    }
}

RC BTNonLeafNode::insert_end(int key, PageId pid)
{
    pages.push_back(pid);
    keys.push_back(key);
    length++;
    return 0;
}

/*
 * Insert the (key, pid) pair to the node
 * and split the node half and half with sibling.
 * The middle key after the split is returned in midKey.
 * @param key[IN] the key to insert
 * @param pid[IN] the PageId to insert
 * @param sibling[IN] the sibling node to split with. This node MUST be empty when this function is called.
 * @param midKey[OUT] the key in the middle after the split. This key should be inserted to the parent node.
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::insertAndSplit(int key, PageId pid, BTNonLeafNode& sibling, int& midKey)
{
    if (length < MAX_KEYS)
        return RC_INVALID_PID;
    insertWithoutCheck(key, pid);
    int half = ceil(MAX_KEYS/2.0);
    std::list<PageId>::iterator pageIt = pages.begin();
    std::list<int>::iterator keyIt = keys.begin();
    for (int i = 0; i < half; i++) {
        pageIt++;
        keyIt++;
    }
    // Save middle key to move up
    midKey = *keyIt;
    keyIt = keys.erase(keyIt);
    // Save corresponding PageId for new lastId
    PageId midPid = *pageIt;
    pageIt = pages.erase(pageIt);
    length--;
    while (keyIt != keys.end()) {
        RC errorCode = sibling.insert_end(*keyIt, *pageIt);
        if (errorCode < 0)
            return errorCode;
        keyIt = keys.erase(keyIt);
        pageIt = pages.erase(pageIt);
        length--;
    }
    sibling.setLastId(lastId);
    lastId = midPid;
    return 0;
}

/*
 * Given the searchKey, find the child-node pointer to follow and
 * output it in pid.
 * @param searchKey[IN] the searchKey that is being looked up.
 * @param pid[OUT] the pointer to the child node to follow.
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::locateChildPtr(int searchKey, PageId& pid)
{ 
    int keyIndex = 0;
    std::list<int>::iterator keyIt = keys.begin();
    std::list<PageId>::iterator pageIt = pages.begin();
    for (; keyIt != keys.end(); keyIt++) {
        if (searchKey < *keyIt) {
            pid = *pageIt;
            return 0;
        }
        pageIt++;
    }
    pid = lastId;
    return 0;
}

/*
 * Initialize the root node with (pid1, key, pid2).
 * @param pid1[IN] the first PageId to insert
 * @param key[IN] the key that should be inserted between the two PageIds
 * @param pid2[IN] the PageId to insert behind the key
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::initializeRoot(PageId pid1, int key, PageId pid2)
{ 
    pages.clear();
    keys.clear();
    isLeaf = 0;
    length = 1;
    pages.push_back(pid1);
    keys.push_back(key);
    lastId = pid2;  
    return 0;
}
