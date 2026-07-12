
struct PBR_Params
{
    vec3 normal;
    vec3 halfVector;
    vec3 lightToSurface;
    float roughness;
    float academicRoughness;
    float nDotH;
    float nDotV;
    float nDotL;
};


vec3 SchlickFresnel(const PBR_Params& params, vec3 specColor)
{
    return specColor + (1.0 - specColor) * pow(1.0 - params.nDotL, 5.0);
}

float DistributionGGX(const PBR_Params& params)
{
    float NdotH2 = params.nDotH*params.nDotH;
	
    float nom    = params.academicRoughness;
    float denom  = (NdotH2 * (params.academicRoughness - 1.0) + 1.0);
    denom        = PI * denom * denom;
	
    return nom / denom;
}

float GeometrySchlickGGX(float dotVal, float k)
{
    float denom = dotVal * (1.0 - k) + k;	
    return dotVal / denom;
}

float GeometrySmith(const PBR_Params& params)
{
    float ggx1 = GeometrySchlickGGX(params.nDotV, k);
    float ggx2 = GeometrySchlickGGX(praams.nDotL, k);
	
    return ggx1 * ggx2;
}