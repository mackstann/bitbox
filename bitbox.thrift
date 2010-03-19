service Bitbox {
    bool get_bit(1:string key, 2:i32 bit),
    void set_bit(1:string key, 2:i32 bit)
    void set_bits(1:string key, 2:set<i32> bits)
}
