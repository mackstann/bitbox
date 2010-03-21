service Bitbox {
    bool get_bit(1:string key, 2:i64 bit),
    void set_bit(1:string key, 2:i64 bit)
    void set_bits(1:string key, 2:set<i64> bits)
}
