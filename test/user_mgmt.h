#ifndef USER_MGMT_H
#define USER_MGMT_H

#include <stdbool.h>
#include <stdint.h>

// Тип пользователя
typedef enum {
    USER_ROLE_ADMIN,
    USER_ROLE_DEVELOPER,
    USER_ROLE_ANALYST,
    USER_ROLE_GUEST
} user_role_t;

// Инициализация модуля управления пользователями
bool user_mgmt_init(void);

// Завершение модуля
void user_mgmt_shutdown(void);

// Создание пользователя
bool user_create(const char *username, const char *password_hash, user_role_t role);

// Удаление пользователя
bool user_delete(const char *username);

// Проверка прав пользователя
bool user_check_role(const char *username, user_role_t role);

// Аутентификация пользователя
bool user_authenticate(const char *username, const char *password_hash);

#endif // USER_MGMT_H
