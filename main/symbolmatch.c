#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <elf.h>

#include "symbolmatch.h"

typedef void(*SM_LOOPFILECALLBACK)(const char* file, uint64_t addr, void* priv);

typedef struct {
    const char* funName;
    const char* fileName;
    uint64_t funAddr;
} SM_CallbackParam;

typedef struct {
    // name = .dynsym && type = 11
    uint32_t dynsymSize;
    void* dynsym;
    // name = .dynstr && type = 3
    uint32_t dynstrSize;
    void* dynstr;
} SM_ElfStruct;

/* ---------------------------------------------------------------- */

static const char* sm_baseName(const char* file)
{
    size_t len = strlen(file);
    if (len < 2)
        return file;
    const char* p = file + len - 2;
    while (p != file)
    {
        if (*p == '/')
            return p + 1;
        p--;
    }
    return p;
}

static void sm_elfRelease(SM_ElfStruct* elf)
{
    if (!elf)
        return;

    if (elf->dynsym)
        free(elf->dynsym);
    
    if (elf->dynstr)
        free(elf->dynstr);

    memset(elf, 0, sizeof(SM_ElfStruct));
}

static int sm_elfLoad(const char* file, SM_ElfStruct* elf)
{
    // Elf32_Ehdr ehdr32;
    // Elf32_Shdr* shdr32 = NULL;

    Elf64_Ehdr ehdr64;
    Elf64_Shdr* shdr64 = NULL;

    uint32_t shstrtabSize = 0;
    char* shstrtab = NULL;
    char* sectionName = NULL;

    FILE* fp;

    uint32_t* tarSize;
    void** tarMem;

    uint32_t i;
    int ret = -1;

    fp = fopen(file, "rb");
    if (!fp)
        return ret;

    do
    {
        /* 32bit */
        if (sizeof(void*) == 4)
        {
            ;
        }
        /* 64bit */
        else
        {
            /* 读 elf 头,取 section header 偏移量 */
            if (fread(&ehdr64, 1, sizeof(Elf64_Ehdr), fp) != sizeof(Elf64_Ehdr))
                break;

            /* 简单格式检查 */
            if (ehdr64.e_shentsize != sizeof(Elf64_Shdr) || !ehdr64.e_shoff || !ehdr64.e_shnum)
                break;

            /* 没有 section header name table 则无法匹配 .strtab 和 .symtab */
            if (ehdr64.e_shstrndx == SHN_XINDEX || ehdr64.e_shstrndx >= ehdr64.e_shnum)
                break;

            /* 全量读取 section header 内存 */
            if (fseek(fp, ehdr64.e_shoff, SEEK_SET) < 0)
                break;
            shdr64 = (Elf64_Shdr*)calloc(ehdr64.e_shnum, sizeof(Elf64_Shdr));
            if (fread(shdr64, 1, ehdr64.e_shnum * sizeof(Elf64_Shdr), fp) != ehdr64.e_shnum * sizeof(Elf64_Shdr))
                break;

            /* 全量读取 shstrtab */
            if (fseek(fp, shdr64[ehdr64.e_shstrndx].sh_offset, SEEK_SET) < 0)
                break;
            shstrtabSize = shdr64[ehdr64.e_shstrndx].sh_size;
            shstrtab = (char*)calloc(shstrtabSize, 1);
            if (fread(shstrtab, 1, shstrtabSize, fp) != shstrtabSize)
                break;

            /* 遍历 section header */
            for (i = 0; i < ehdr64.e_shnum; i++)
            {
                sectionName = shstrtab + shdr64[i].sh_name;

                /* 匹配目标 section */
                if (shdr64[i].sh_type == SHT_DYNSYM &&
                    shdr64[i].sh_name < shstrtabSize &&
                    !strcmp((const char*)".dynsym", sectionName))
                {
                    tarSize = &elf->dynsymSize;
                    tarMem = &elf->dynsym;
                }
                else if (shdr64[i].sh_type == SHT_STRTAB &&
                    shdr64[i].sh_name < shstrtabSize &&
                    !strcmp((const char*)".dynstr", sectionName))
                {
                    tarSize = &elf->dynstrSize;
                    tarMem = &elf->dynstr;
                }
                else
                    continue;

                /* 跳转 section 地址 */
                if (fseek(fp, shdr64[i].sh_offset, SEEK_SET) < 0)
                    break;

                /* section 全量读取 */
                *tarSize = (uint32_t)shdr64[i].sh_size;
                *tarMem = calloc(*tarSize, 1);
                if (fread(*tarMem, 1, *tarSize, fp) != *tarSize)
                    break;
            }
        }

        ret = 0;
    } while (0);

    if (shstrtab)
        free(shstrtab);

    // if (shdr32)
    //     free(shdr32);
    if (shdr64)
        free(shdr64);

    fclose(fp);
    return ret;
}

static uint64_t sm_getGlobalFunAddr(const char* file, const char* funName)
{
    uint32_t i;
    uint64_t ret = 0;
    uint32_t* up32;
    const char* symbol;

    SM_ElfStruct elf = {0};
    sm_elfLoad(file, &elf);

    /* 32bit or 64bit system ? */
    if (sizeof(void*) == 4)
    {
        ;
    }
    else
    {
        /* check .dynsym */
        if (elf.dynsym && elf.dynstr)
        {
            for (i = 0, up32 = (uint32_t*)elf.dynsym; i < elf.dynsymSize; i += sizeof(Elf64_Sym))
            {
                /* 匹配名称,取得地址 */
                symbol = ((const char*)elf.dynstr) + up32[0];
                // printf("match: %s -> %s \r\n", funName, symbol);
                if (!strcmp(funName, symbol))
                {
                    ret = up32[2];
                    break;
                }
                up32 += sizeof(Elf64_Sym) / 4;
            }
        }
    }

    sm_elfRelease(&elf);
    return ret;
}

static void sm_loopMapFile(SM_LOOPFILECALLBACK callback, void* priv)
{
    const char* selfmaps = "/proc/self/maps";
    FILE* fp = NULL;

    char currFile[1024] = {0};
    char prevFile[1024] = {0};
    char line[1024] = {0};
    char tmp[128] = {0};
    uint64_t addr = 0;
    size_t ret = 0;

    fp = fopen(selfmaps, "rb");
    if (!fp)
    {
        fprintf(stderr, "%s: open %s failed\r\n", __FUNCTION__, selfmaps);
        return ;
    }

    while (fgets(line, sizeof(line), fp))
    {
        memset(currFile, 0, sizeof(currFile));
        ret = sscanf(line, "%lX-%127s %127s %127s %127s %127s %1023s", &addr, tmp, tmp, tmp, tmp, tmp, currFile);
        if (ret != 7)
            continue;
        
        if (!strcmp(currFile, prevFile))
            continue;
        strcpy(prevFile, currFile);
        
        callback(currFile, addr, priv);
    }

    fclose(fp);
}

/* ---------------------------------------------------------------- */

static void sm_loopFileCallback(const char* file, uint64_t addr, void* priv)
{
    SM_CallbackParam* param = (SM_CallbackParam*)priv;
    const char* fileName = sm_baseName(file);
    uint64_t ret = 0;

    // printf("loop file: addr: %10lX, file: %s \r\n", addr, file);

    /* 匹配文件名 */
    if (!param->fileName || !strcmp(param->fileName, fileName))
    {
        // printf("    hit: %s - %s - %s\r\n", param->funName, param->fileName, fileName);

        /* 在elf的全局符号表中匹配函数地址 */
        ret = sm_getGlobalFunAddr(file, param->funName);
        /* 返回地址有效,加上内存起始地址,得到最终结果 */
        if (ret)
            param->funAddr = ret + addr;
    }
}

uint64_t sm_getFunAddr(const char* funName, const char* fileName)
{
    SM_CallbackParam param = {funName, fileName, 0};
    sm_loopMapFile(sm_loopFileCallback, &param);
    return param.funAddr;
}
