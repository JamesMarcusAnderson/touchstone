/* Platform detection uses names the compiler defines. */
int platform_code(void)
{
    int code = 0;
#ifdef __linux__
    code = 1;
#endif
#ifdef _WIN32
    code = 2;
#endif
    return code;
}

int aligned_slot(void)
{
    void *slot = 0;
    return posix_memalign(&slot, 64, 128);
}
