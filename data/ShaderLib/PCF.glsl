
float PCF(sampler2D tex, const vec2 coords, const vec2 base, const vec2 ext, float compareDist, float lightRad)
{	
    
	float offset = (1.0 / textureSize(tex, 0).x);
    
	float shadow = texture(tex, coords).r * lightRad + 0.5 > compareDist ? 1.0 : 0.0;
    
    vec2 sCoords = coords + vec2(-0.866 * offset,  0.5 * offset);
    sCoords = clamp(sCoords, base, ext);
	shadow += texture(tex, sCoords).r  * lightRad + 0.5 > compareDist ? 1.0 : 0.0;

    sCoords = coords + vec2(-0.866 * offset, -0.5 * offset);
    sCoords = clamp(sCoords, base, ext);
	shadow += texture(tex, sCoords).r  * lightRad + 0.5 > compareDist ? 1.0 : 0.0;

    sCoords = coords + vec2(0.866 * offset, -0.5 * offset);
    sCoords = clamp(sCoords, base, ext);
	shadow += texture(tex, sCoords).r  * lightRad + 0.5 > compareDist ? 1.0 : 0.0;
    
    sCoords = coords + vec2(0.866 * offset,  0.5 * offset);
    sCoords = clamp(sCoords, base, ext);
	shadow += texture(tex, sCoords).r  * lightRad + 0.5 > compareDist ? 1.0 : 0.0;
    
	return shadow / 5.0;
}

float PCFShadow(sampler2DShadow tex, const vec2 coords, const vec2 base, const vec2 ext, float compareDist, float lightRad)
{	
    
	float offset = (1.0 / textureSize(tex, 0).x);
    
	float shadow = texture(tex, vec3(coords, compareDist)).r;
    
    vec2 sCoords = coords + vec2(-0.866 * offset,  0.5 * offset);
    sCoords = clamp(sCoords, base, ext);
	shadow += texture(tex, vec3(sCoords, compareDist));

    sCoords = coords + vec2(-0.866 * offset, -0.5 * offset);
    sCoords = clamp(sCoords, base, ext);
	shadow += texture(tex, vec3(sCoords, compareDist));

    sCoords = coords + vec2(0.866 * offset, -0.5 * offset);
    sCoords = clamp(sCoords, base, ext);
	shadow += texture(tex, vec3(sCoords, compareDist));
    
    sCoords = coords + vec2(0.866 * offset,  0.5 * offset);
    sCoords = clamp(sCoords, base, ext);
	shadow += texture(tex, vec3(sCoords, compareDist));
    
	return shadow / 5.0;
}