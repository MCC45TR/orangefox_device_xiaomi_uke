// SPDX-License-Identifier: Apache-2.0
// Host-only static instancing. Retains every encoded character and layout table.
#include <hb.h>
#include <hb-subset.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::runtime_error("Usage: font-instance INPUT FACE_INDEX OUTPUT");
        const auto index=static_cast<unsigned>(std::stoul(argv[2]));
        auto* blob=hb_blob_create_from_file_or_fail(argv[1]);
        if(!blob)throw std::runtime_error("Cannot read font");
        auto* face=hb_face_create(blob,index);
        if(!hb_face_get_glyph_count(face))throw std::runtime_error("Invalid face index");
        auto* input=hb_subset_input_create_or_fail();
        if(!input)throw std::runtime_error("Cannot create font input");
        hb_set_invert(hb_subset_input_unicode_set(input));
        if(!hb_subset_input_pin_axis_location(input,face,HB_TAG('w','g','h','t'),400))
            throw std::runtime_error("Font lacks the expected weight axis");
        auto* output=hb_subset_or_fail(face,input);
        if(!output)throw std::runtime_error("Static font instancing failed");
        auto* result=hb_face_reference_blob(output); unsigned size=0;
        const auto* bytes=hb_blob_get_data(result,&size);
        std::ofstream file(argv[3],std::ios::binary|std::ios::trunc); file.write(bytes,size);
        if(!file || !size)throw std::runtime_error("Cannot save font");
        std::cout<<"Static font: "<<size<<" bytes; complete character set retained\n";
        hb_blob_destroy(result); hb_face_destroy(output); hb_subset_input_destroy(input);
        hb_face_destroy(face); hb_blob_destroy(blob); return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
