/**
 * @file factoryDatabase.h
 * @author Ruilin Wu(202222080631@std.uestc.edu.cn)
 * @brief the factory of database
 * @version 0.1
 * @date 2023-01-26
 * 
 * @copyright Copyright (c) 2020
 * 
 */

#ifndef BASICDEDUP_DATABASE_FACTORY_H
#define BASICDEDUP_DATABASE_FACTORY_H

#include "absDatabase.h"
#include "leveldbDatabase.h"
#include "inMemoryDatabase.h"

#define LEVEL_DB 1
#define ROCKS_DB 2
#define IN_MEMORY 3



class DatabaseFactory {
    private:

    public:
        AbsDatabase* CreateDatabase(int type, string path);
};

#endif // !BASICDEDUP_DATABASE_FACTORY_H

