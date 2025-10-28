SECTIONS
{
    .test :
    {
        _ep_test_start = .;
        KEEP(*(.ep_test));
        _ep_test_end = .;
    }

}
INSERT AFTER .rodata;
