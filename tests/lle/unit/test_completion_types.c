/*
 * Lush Shell - LLE Completion Types Unit Tests
 * Copyright (C) 2021-2026  Michael Berry
 *
 * Licensed under the MIT License. See LICENSE file for details.
 */

#include "lle/completion/completion_types.h"
#include "lle/memory_management.h"
#include "test_framework.h"

#include <stdio.h>
#include <string.h>

TEST(type_info_queries) {
    const lle_completion_type_info_t *info =
        lle_completion_type_get_info(LLE_COMPLETION_TYPE_BUILTIN);
    ASSERT(info != NULL);
    ASSERT(info->type == LLE_COMPLETION_TYPE_BUILTIN);
    ASSERT(strcmp(info->type_name, "Built-in") == 0);
    ASSERT(strcmp(info->category_name, "BUILT-INS") == 0);
    ASSERT(info->default_priority == 900);

    const char *category =
        lle_completion_type_get_category(LLE_COMPLETION_TYPE_COMMAND);
    ASSERT(strcmp(category, "COMMANDS") == 0);

    const char *indicator =
        lle_completion_type_get_indicator(LLE_COMPLETION_TYPE_FILE);
    ASSERT(indicator != NULL);

    info = lle_completion_type_get_info((lle_completion_type_t)999);
    ASSERT(info != NULL);
    ASSERT(info->type == LLE_COMPLETION_TYPE_UNKNOWN);
}

TEST(completion_item_lifecycle) {
    /* LLE uses a global memory pool — pass a dummy non-NULL pool. */
    lle_memory_pool_t *pool = (lle_memory_pool_t *)1;

    lle_completion_item_t *item = NULL;
    lle_result_t result = lle_completion_item_create(
        pool, "test_command", " ", LLE_COMPLETION_TYPE_COMMAND, 800, &item);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(item != NULL);
    ASSERT(item->text != NULL);
    ASSERT(strcmp(item->text, "test_command") == 0);
    ASSERT(item->suffix != NULL);
    ASSERT(strcmp(item->suffix, " ") == 0);
    ASSERT(item->type == LLE_COMPLETION_TYPE_COMMAND);
    ASSERT(item->relevance_score == 800);
    ASSERT(item->owns_text == true);
    ASSERT(item->owns_suffix == true);

    result = lle_completion_item_free(pool, item);
    ASSERT(result == LLE_SUCCESS);
}

TEST(completion_item_with_description) {
    lle_memory_pool_t *pool = (lle_memory_pool_t *)1;

    lle_completion_item_t *item = NULL;
    lle_result_t result = lle_completion_item_create_with_description(
        pool, "ls", " ", LLE_COMPLETION_TYPE_BUILTIN, 900,
        "List directory contents", &item);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(item != NULL);
    ASSERT(item->description != NULL);
    ASSERT(strcmp(item->description, "List directory contents") == 0);
    ASSERT(item->owns_description == true);

    lle_completion_item_free(pool, item);
}

TEST(completion_result_lifecycle) {
    lle_memory_pool_t *pool = (lle_memory_pool_t *)1;

    lle_completion_result_t *result = NULL;
    lle_result_t res = lle_completion_result_create(pool, 4, &result);

    ASSERT(res == LLE_SUCCESS);
    ASSERT(result != NULL);
    ASSERT(result->count == 0);
    ASSERT(result->capacity == 4);
    ASSERT(result->items != NULL);
    ASSERT(result->memory_pool == pool);

    res = lle_completion_result_add(result, "cd", " ",
                                    LLE_COMPLETION_TYPE_BUILTIN, 900);
    ASSERT(res == LLE_SUCCESS);
    ASSERT(result->count == 1);
    ASSERT(result->builtin_count == 1);

    res = lle_completion_result_add(result, "ls", " ",
                                    LLE_COMPLETION_TYPE_COMMAND, 800);
    ASSERT(res == LLE_SUCCESS);
    ASSERT(result->count == 2);
    ASSERT(result->command_count == 1);

    res = lle_completion_result_add(result, "file.txt", "",
                                    LLE_COMPLETION_TYPE_FILE, 600);
    ASSERT(res == LLE_SUCCESS);
    ASSERT(result->count == 3);
    ASSERT(result->file_count == 1);

    res = lle_completion_result_add(result, "dir/", "/",
                                    LLE_COMPLETION_TYPE_DIRECTORY, 700);
    ASSERT(res == LLE_SUCCESS);
    ASSERT(result->count == 4);

    /* Adding 5th item triggers capacity growth (capacity was 4). */
    res = lle_completion_result_add(result, "$HOME", "",
                                    LLE_COMPLETION_TYPE_VARIABLE, 500);
    ASSERT(res == LLE_SUCCESS);
    ASSERT(result->count == 5);
    ASSERT(result->capacity >= 5);

    const lle_completion_item_t *item =
        lle_completion_result_get_item(result, 0);
    ASSERT(item != NULL);
    ASSERT(strcmp(item->text, "cd") == 0);

    size_t builtin_count = lle_completion_result_count_by_type(
        result, LLE_COMPLETION_TYPE_BUILTIN);
    ASSERT(builtin_count == 1);

    res = lle_completion_result_free(result);
    ASSERT(res == LLE_SUCCESS);
}

TEST(completion_result_sorting) {
    lle_memory_pool_t *pool = (lle_memory_pool_t *)1;

    lle_completion_result_t *result = NULL;
    lle_completion_result_create(pool, 8, &result);

    lle_completion_result_add(result, "file1.txt", "", LLE_COMPLETION_TYPE_FILE,
                              500);
    lle_completion_result_add(result, "cd", " ", LLE_COMPLETION_TYPE_BUILTIN,
                              950);
    lle_completion_result_add(result, "ls", " ", LLE_COMPLETION_TYPE_COMMAND,
                              800);
    lle_completion_result_add(result, "echo", " ", LLE_COMPLETION_TYPE_BUILTIN,
                              900);

    lle_result_t res = lle_completion_result_sort(result);
    ASSERT(res == LLE_SUCCESS);

    const lle_completion_item_t *item0 =
        lle_completion_result_get_item(result, 0);
    const lle_completion_item_t *item1 =
        lle_completion_result_get_item(result, 1);
    const lle_completion_item_t *item2 =
        lle_completion_result_get_item(result, 2);
    const lle_completion_item_t *item3 =
        lle_completion_result_get_item(result, 3);

    ASSERT(item0->type == LLE_COMPLETION_TYPE_BUILTIN);
    ASSERT(item1->type == LLE_COMPLETION_TYPE_BUILTIN);
    ASSERT(item0->relevance_score >= item1->relevance_score);
    ASSERT(item2->type == LLE_COMPLETION_TYPE_COMMAND);
    ASSERT(item3->type == LLE_COMPLETION_TYPE_FILE);

    lle_completion_result_free(result);
}

TEST(classification) {
    lle_completion_type_t type = lle_completion_classify_text("$HOME", false);
    ASSERT(type == LLE_COMPLETION_TYPE_VARIABLE);

    type = lle_completion_classify_text("dir/file", false);
    ASSERT(type == LLE_COMPLETION_TYPE_FILE ||
           type == LLE_COMPLETION_TYPE_DIRECTORY);

    type = lle_completion_classify_text("somecommand", true);
    ASSERT(type == LLE_COMPLETION_TYPE_COMMAND);
}

TEST(error_handling) {
    lle_memory_pool_t *pool = (lle_memory_pool_t *)1;

    lle_completion_item_t *item = NULL;
    lle_result_t result = lle_completion_item_create(
        NULL, "test", " ", LLE_COMPLETION_TYPE_COMMAND, 800, &item);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER);

    result = lle_completion_item_create(
        pool, NULL, " ", LLE_COMPLETION_TYPE_COMMAND, 800, &item);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER);

    result = lle_completion_item_create(pool, "test", " ",
                                        LLE_COMPLETION_TYPE_COMMAND, 800, NULL);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER);

    lle_completion_result_t *comp_result = NULL;
    result = lle_completion_result_create(NULL, 16, &comp_result);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER);

    result = lle_completion_result_create(pool, 16, NULL);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER);
}

int main(void) {
    printf("=== LLE Completion Types Unit Tests ===\n");
    RUN_TEST(type_info_queries);
    RUN_TEST(completion_item_lifecycle);
    RUN_TEST(completion_item_with_description);
    RUN_TEST(completion_result_lifecycle);
    RUN_TEST(completion_result_sorting);
    RUN_TEST(classification);
    RUN_TEST(error_handling);
    return TEST_RESULT();
}
