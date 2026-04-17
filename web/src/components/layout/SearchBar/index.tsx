import { Box, IconButton, Paper } from '@mui/material';
import TextField from '@mui/material/TextField';
import SearchIcon from '@mui/icons-material/Search';
import styles from './styles.module.sass';
import clsx from 'clsx';
import { useState } from 'react';
import { useBooksStore } from '../../../store/books';

const ICON_SIZE = 30;
const iconSx = { fontSize: ICON_SIZE };

const SearchBar = () => {
    const query = useBooksStore((state) => state.query);
    const setQuery = useBooksStore((state) => state.setQuery);

    const [mobileOpen, setMobileOpen] = useState(false);

    return (
        <Box sx={{ p: 1 }}>
            <Box className={clsx('display__grid', styles.searchBar)}>
                <TextField
                    placeholder='Search'
                    variant='outlined'
                    size='small'
                    value={query}
                    onChange={(e) => setQuery(e.target.value)}
                    sx={{ '& fieldset': { border: 'none' } }}
                    className={clsx('display__none--xs')}
                />

                <IconButton
                    onClick={() => setMobileOpen(true)}
                    className={clsx('display__none--md')}
                >
                    <SearchIcon sx={iconSx} />
                </IconButton>

                <IconButton
                    onClick={() => {}}
                    className={clsx('display__none--xs')}
                >
                    <SearchIcon sx={iconSx} />
                </IconButton>

                {mobileOpen && (
                    <Paper
                        className={clsx(
                            'position__absolute',
                            'height__100',
                            'width__100',
                            'display__grid',
                            'align-items__center',
                            'top__0',
                            'left__0',
                            styles.searchBar
                        )}
                    >
                        <TextField
                            placeholder='Search'
                            variant='outlined'
                            size='small'
                            value={query}
                            onChange={(e) => setQuery(e.target.value)}
                            autoFocus
                            sx={{ '& fieldset': { border: 'none' } }}
                        />
                        <Box sx={{ p: 1 }}>
                            <IconButton onClick={() => setMobileOpen(false)}>
                                <SearchIcon sx={iconSx} />
                            </IconButton>
                        </Box>
                    </Paper>
                )}
            </Box>
        </Box>
    );
};

export default SearchBar;